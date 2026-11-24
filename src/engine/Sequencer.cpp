// SignalFlux — sequencing engine (tasks SF-003, SF-004, SF-005).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "engine/Sequencer.h"

#include "engine/LegTiming.h"

#include <algorithm>

namespace signalflux::engine {

namespace {

/// D14: Stop takes effect on a MIDI Clock pulse boundary.
MusicalTick ceilToClock(MusicalTick t) noexcept {
    const auto v = t.value();
    const auto r = v % kTicksPerMidiClock;
    return MusicalTick{r == 0 ? v : v + (kTicksPerMidiClock - r)};
}

EngineEvent makeEvent(EngineEventType type, MusicalTick t, TravelerId traveler = {}, StationId station = {}) noexcept {
    EngineEvent e;
    e.type = type;
    e.tick = t;
    e.traveler = traveler;
    e.station = station;
    return e;
}

/// Value draws for Random Value Mode come from the Station's own stream (D10).
class StationValueDraw final : public IntDraw {
public:
    StationValueDraw(RandomStreams& r, std::size_t index, StationId id) : r_(r), index_(index), id_(id) {}
    std::int64_t draw(std::int64_t lo, std::int64_t hi) noexcept override {
        return r_.between(index_, id_, RandomPurpose::Value, lo, hi);
    }

private:
    RandomStreams& r_;
    std::size_t index_;
    StationId id_;
};

} // namespace

Sequencer::Sequencer(IEngineEventSink& sink) : sink_(sink) {
    travelers_.reserve(limits::kMaxTravelerCap);
    dueScratch_.reserve(limits::kMaxTravelerCap);
    notes_.reset(limits::kMaxActiveNoteCap);
}

std::size_t Sequencer::travelerCap() const noexcept {
    return graph_ ? std::min<std::size_t>(graph_->settings().travelerCap, limits::kMaxTravelerCap) : 0;
}

// ---------------------------------------------------------------------------
// Document edits (D3, D4, D5, D21 step 1)
// ---------------------------------------------------------------------------

ValidationReport Sequencer::setDocument(const ProjectDocument& doc) {
    ValidationReport report;
    auto compiled = RuntimeGraph::compile(doc, &report);
    if (!compiled) return report;

    if (graph_) {
        for (std::size_t i = 0; i < travelers_.size();) {
            Traveler& tr = travelers_[i];
            if (tr.phase == Phase::Dwelling) {
                const StationId at = graph_->stations()[tr.station].id;
                if (const auto ni = compiled->stationIndex(at)) {
                    tr.station = *ni;
                    ++i;
                } else {
                    auto e = makeEvent(EngineEventType::TravelerEnd, now_, tr.id, at);
                    e.endReason = EndReason::StationDeleted;
                    emit(e);
                    travelers_[i] = travelers_.back();
                    travelers_.pop_back();
                }
                continue;
            }
            const RuntimeFlowpoint& oldFp = graph_->flowpoints()[tr.flowpoint];
            const auto newIndex = compiled->flowpointIndex(oldFp.id);
            if (!newIndex) {
                const StationId to = graph_->stations()[oldFp.to].id;
                auto e = makeEvent(EngineEventType::TravelerEnd, now_, tr.id, to);
                e.flowpoint = oldFp.id;
                e.endReason = compiled->stationIndex(to) ? EndReason::RouteDeleted : EndReason::StationDeleted;
                emit(e);
                travelers_[i] = travelers_.back();
                travelers_.pop_back();
                continue;
            }
            tr.flowpoint = *newIndex;
            tr.deadline = recomputeDeadline(tr.legStart, compiled->flowpoints()[*newIndex].duration, now_);
            ++i;
        }

        // Round-robin state and random-stream counters survive edits for
        // Stations that still exist (D6, D10).
        std::vector<std::uint32_t> remapped(compiled->stations().size(), 0);
        std::vector<std::ptrdiff_t> newToOld(compiled->stations().size(), -1);
        for (std::size_t i = 0; i < graph_->stations().size(); ++i)
            if (const auto ni = compiled->stationIndex(graph_->stations()[i].id)) {
                remapped[*ni] = roundRobin_[i];
                newToOld[*ni] = static_cast<std::ptrdiff_t>(i);
            }
        roundRobin_ = std::move(remapped);
        random_.remap(newToOld);
    } else {
        roundRobin_.assign(compiled->stations().size(), 0);
        random_.reset(compiled->settings().randomSeed, compiled->stations().size());
    }

    std::size_t maxOut = 0;
    for (const auto& s : compiled->stations()) maxOut = std::max(maxOut, s.outgoing.size());
    routeScratch_.reserve(maxOut);

    graph_ = std::move(compiled);
    notes_.setCapacity(graph_->settings().activeNoteCap);
    return report;
}

// ---------------------------------------------------------------------------
// Transport (D14)
// ---------------------------------------------------------------------------

void Sequencer::play(std::optional<std::uint64_t> seed) {
    if (!graph_) return;
    if (transport_ != TransportState::Stopped) releaseAllNotes(); // never leave stuck notes

    now_ = MusicalTick{0};
    stopAt_.reset();
    travelers_.clear();
    std::fill(roundRobin_.begin(), roundRobin_.end(), 0u);
    notes_.reset(graph_->settings().activeNoteCap);
    random_.reset(seed.value_or(graph_->settings().randomSeed), graph_->stations().size());
    nextTravelerId_ = 1;
    stats_ = {};
    transport_ = TransportState::Playing;

    emit(makeEvent(EngineEventType::TransportStart, now_));

    // Starters: trigger immediately at tick 0, then repeat or depart (BEHAVIORAL §1).
    for (const StationIndex s : graph_->startStations()) {
        const StationId sid = graph_->stations()[s].id;
        if (travelers_.size() >= travelerCap()) {
            ++stats_.droppedTravelers;
            emit(makeEvent(EngineEventType::TravelerDropped, now_, {}, sid));
            continue;
        }
        Traveler tr;
        tr.id = newTravelerId();
        tr.legStart = now_;
        tr.deadline = now_;
        travelers_.push_back(tr);
        emit(makeEvent(EngineEventType::TravelerSpawn, now_, tr.id, sid));
        enterStation(travelers_.size() - 1, s, now_);
        stats_.travelerHighWater = std::max(stats_.travelerHighWater, travelers_.size());
    }
}

void Sequencer::requestStop() {
    if (transport_ != TransportState::Playing) return;
    stopAt_ = ceilToClock(now_);
    if (*stopAt_ == now_) performStop();
}

void Sequencer::performStop() {
    releaseAllNotes();
    silencePendingRepeats();
    emit(makeEvent(EngineEventType::TransportStop, now_));
    stopAt_.reset();
    transport_ = TransportState::Paused;
}

void Sequencer::continuePlayback() {
    if (transport_ != TransportState::Paused) return;
    emit(makeEvent(EngineEventType::TransportContinue, now_));
    transport_ = TransportState::Playing;
}

void Sequencer::panic() {
    releaseAllNotes();
    silencePendingRepeats();
    emit(makeEvent(EngineEventType::Panic, now_));
}

/// D8: Stop or Panic discards a Repeat group's remaining triggers. The dwell
/// time itself is kept, so routing and timing are unchanged (P2).
void Sequencer::silencePendingRepeats() noexcept {
    for (auto& tr : travelers_)
        if (tr.phase == Phase::Dwelling) tr.silent = true;
}

void Sequencer::releaseAllNotes() {
    notes_.releaseAll([this](const ActiveNoteRegistry::Release& r) {
        auto off = makeEvent(EngineEventType::NoteOff, now_);
        off.channel = r.channel;
        off.note = r.note;
        emit(off);
    });
}

// ---------------------------------------------------------------------------
// Scheduling
// ---------------------------------------------------------------------------

std::optional<MusicalTick> Sequencer::nextEventTick() const noexcept {
    std::optional<MusicalTick> next = notes_.nextDue();
    for (const auto& tr : travelers_)
        if (!next || tr.deadline < *next) next = tr.deadline;
    return next;
}

void Sequencer::processUntil(MusicalTick horizon) {
    if (transport_ != TransportState::Playing || horizon < now_) return;

    while (true) {
        MusicalTick limit = horizon;
        if (stopAt_ && *stopAt_ < limit) limit = *stopAt_;

        const auto next = nextEventTick();
        if (next && *next < limit) {
            now_ = *next;
            processTick(now_);
            continue;
        }
        now_ = limit;
        if (stopAt_ && now_ == *stopAt_) performStop();
        return;
    }
}

void Sequencer::processTick(MusicalTick t) {
    // D21 step 2: Note Offs before Note Ons.
    notes_.releaseDue(t, [this, t](const ActiveNoteRegistry::Release& r) {
        auto off = makeEvent(EngineEventType::NoteOff, t);
        off.channel = r.channel;
        off.note = r.note;
        emit(off);
    });

    // D21 step 3: arrivals and Repeat triggers in ascending TravelerId order.
    dueScratch_.clear();
    for (const auto& tr : travelers_)
        if (tr.deadline == t) dueScratch_.push_back(tr.id);
    std::sort(dueScratch_.begin(), dueScratch_.end());

    for (const TravelerId id : dueScratch_) {
        const auto it = std::find_if(travelers_.begin(), travelers_.end(), [id](const Traveler& tr) { return tr.id == id; });
        if (it == travelers_.end()) continue;
        const auto index = static_cast<std::size_t>(it - travelers_.begin());

        if (it->phase == Phase::Dwelling) {
            repeatTrigger(index, t);
            continue;
        }
        const StationIndex s = graph_->flowpoints()[it->flowpoint].to;
        emit(makeEvent(EngineEventType::TravelerArrive, t, id, graph_->stations()[s].id));
        enterStation(index, s, t);
    }

    stats_.travelerHighWater = std::max(stats_.travelerHighWater, travelers_.size());
}

bool Sequencer::isSilent(StationIndex s) noexcept {
    const RuntimeStation& st = graph_->stations()[s];
    if (st.manuallyMuted) return true;
    // D7: probability is evaluated once per arrival; 0 and 100 consume no draw.
    const int p = st.probability.value();
    if (p == 0) return true;
    if (p == 100) return false;
    return random_.below(s, st.id, RandomPurpose::Probability, 100) >= static_cast<std::uint64_t>(p);
}

void Sequencer::enterStation(std::size_t index, StationIndex s, MusicalTick t) {
    const RuntimeStation& st = graph_->stations()[s];
    const bool silent = isSilent(s);

    ResolvedNotes resolved;
    if (st.eventType == StationEventType::Note) {
        // Resolution always runs, even when silent, so the melodic context and
        // the random streams do not depend on mute or probability (P2, D19).
        StationValueDraw draw{random_, s, st.id};
        resolved = resolveNotes(st.note, ScaleContext{graph_->settings().root, graph_->settings().scale},
                                travelers_[index].context, draw);
        travelers_[index].context = resolved.outgoing;
        if (!silent) emitNotes(s, resolved, travelers_[index].id, t);
    }
    // CC and Pitch Bend Stations (D26) emit in SF-007 and never touch note context.

    if (st.additionalRepeats > 0) {
        Traveler& tr = travelers_[index];
        tr.phase = Phase::Dwelling;
        tr.station = s;
        tr.repeatNotes = resolved;
        tr.silent = silent;
        tr.repeatsLeft = st.additionalRepeats;
        tr.repeatInterval = st.repeatInterval;
        tr.legStart = t;
        tr.deadline = t + st.repeatInterval;
        return;
    }
    departOrEnd(index, s, t);
}

void Sequencer::repeatTrigger(std::size_t index, MusicalTick t) {
    Traveler& tr = travelers_[index];
    const StationIndex s = tr.station;
    emit(makeEvent(EngineEventType::TravelerRepeat, t, tr.id, graph_->stations()[s].id));
    if (!tr.silent) emitNotes(s, tr.repeatNotes, tr.id, t);
    if (--tr.repeatsLeft > 0) {
        tr.deadline = t + tr.repeatInterval;
        return;
    }
    departOrEnd(index, s, t); // D8: departure coincides with the final trigger
}

void Sequencer::emitNotes(StationIndex s, const ResolvedNotes& resolved, TravelerId traveler, MusicalTick t) {
    const RuntimeStation& st = graph_->stations()[s];
    for (std::size_t i = 0; i < resolved.count; ++i) { // ascending pitch order (D34)
        const MidiNote pitch = resolved.pitches[i];
        if (!notes_.add(st.channel, pitch, t + st.note.duration)) {
            ++stats_.droppedNotes;
            auto d = makeEvent(EngineEventType::NoteDropped, t, {}, st.id);
            d.channel = st.channel;
            d.note = pitch;
            emit(d);
            continue;
        }
        stats_.noteHighWater = std::max(stats_.noteHighWater, notes_.size());
        auto on = makeEvent(EngineEventType::NoteOn, t, traveler, st.id);
        on.channel = st.channel;
        on.note = pitch;
        on.velocity = st.note.velocity;
        emit(on);
    }
}

void Sequencer::endTraveler(std::size_t index, StationId at, EndReason why, MusicalTick t) {
    auto e = makeEvent(EngineEventType::TravelerEnd, t, travelers_[index].id, at);
    e.endReason = why;
    emit(e);
    travelers_[index] = travelers_.back();
    travelers_.pop_back();
}

void Sequencer::departOrEnd(std::size_t index, StationIndex s, MusicalTick t) {
    chooseRoutes(s);
    if (routeScratch_.empty()) { // D24 (and D32: all weights zero)
        endTraveler(index, graph_->stations()[s].id, EndReason::NoRoute, t);
        return;
    }
    Traveler& tr = travelers_[index];
    tr.phase = Phase::Travelling;
    depart(tr, s, routeScratch_.front(), t);
    spawnChildren(s, index, 1, t);
}

void Sequencer::chooseRoutes(StationIndex s) {
    routeScratch_.clear();
    const RuntimeStation& st = graph_->stations()[s];
    const auto n = st.outgoing.size();
    if (n == 0) return;

    switch (st.branchMode) {
        case BranchMode::Instant:
            routeScratch_.assign(st.outgoing.begin(), st.outgoing.end()); // within reserved capacity
            return;
        case BranchMode::Random: { // D32: weighted, seeded, integer draw
            std::uint64_t total = 0;
            for (const auto f : st.outgoing) total += static_cast<std::uint64_t>(graph_->flowpoints()[f].weight.value());
            if (total == 0) return;
            std::uint64_t pick = random_.below(s, st.id, RandomPurpose::Branch, total);
            for (const auto f : st.outgoing) {
                const auto w = static_cast<std::uint64_t>(graph_->flowpoints()[f].weight.value());
                if (pick < w) {
                    routeScratch_.push_back(f);
                    return;
                }
                pick -= w;
            }
            return;
        }
        case BranchMode::Sequential: {
            std::uint32_t& next = roundRobin_[s];
            if (next >= n) next = 0; // D6: counter reset after deletions
            routeScratch_.push_back(st.outgoing[next]);
            next = static_cast<std::uint32_t>((next + 1) % n);
            return;
        }
    }
}

void Sequencer::depart(Traveler& traveler, StationIndex from, FlowpointIndex route, MusicalTick t) {
    traveler.phase = Phase::Travelling;
    traveler.flowpoint = route;
    traveler.legStart = t;
    traveler.deadline = t + graph_->flowpoints()[route].duration;

    auto e = makeEvent(EngineEventType::TravelerDepart, t, traveler.id, graph_->stations()[from].id);
    e.flowpoint = graph_->flowpoints()[route].id;
    e.deadline = traveler.deadline;
    emit(e);
}

void Sequencer::spawnChildren(StationIndex from, std::size_t parentIndex, std::size_t firstRoute, MusicalTick t) {
    // Instant branching: one new Traveler per extra route, in outgoing order (D21).
    // Over the cap, the excess is dropped from the end of outgoing order (D23).
    for (std::size_t r = firstRoute; r < routeScratch_.size(); ++r) {
        if (travelers_.size() >= travelerCap()) {
            ++stats_.droppedTravelers;
            emit(makeEvent(EngineEventType::TravelerDropped, t, {}, graph_->stations()[from].id));
            continue;
        }
        Traveler child;
        child.id = newTravelerId();
        child.context = travelers_[parentIndex].context; // D19: children inherit context
        travelers_.push_back(child);
        depart(travelers_.back(), from, routeScratch_[r], t);
    }
}

EngineSnapshot Sequencer::snapshot() const {
    EngineSnapshot snap;
    snap.now = now_;
    snap.transport = transport_;
    snap.stats = stats_;
    if (!graph_) return snap;
    snap.travelers.reserve(travelers_.size());
    for (const auto& tr : travelers_) {
        TravelerView v;
        v.id = tr.id;
        v.legStart = tr.legStart;
        v.deadline = tr.deadline;
        if (tr.phase == Phase::Dwelling) {
            v.dwelling = true;
            v.from = v.to = graph_->stations()[tr.station].id;
        } else {
            const auto& fp = graph_->flowpoints()[tr.flowpoint];
            v.flowpoint = fp.id;
            v.from = graph_->stations()[fp.from].id;
            v.to = graph_->stations()[fp.to].id;
        }
        snap.travelers.push_back(v);
    }
    std::sort(snap.travelers.begin(), snap.travelers.end(),
              [](const TravelerView& a, const TravelerView& b) { return a.id < b.id; });
    return snap;
}

} // namespace signalflux::engine
