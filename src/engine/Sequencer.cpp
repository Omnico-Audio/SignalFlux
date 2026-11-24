// SignalFlux — sequencing engine (task SF-003).
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

    // Reconcile in-flight Travelers against the new geometry.
    if (graph_) {
        for (std::size_t i = 0; i < travelers_.size();) {
            Traveler& tr = travelers_[i];
            const RuntimeFlowpoint& oldFp = graph_->flowpoints()[tr.flowpoint];
            const auto newIndex = compiled->flowpointIndex(oldFp.id);
            if (!newIndex) {
                const StationId to = graph_->stations()[oldFp.to].id;
                EngineEvent e;
                e.tick = now_;
                e.type = EngineEventType::TravelerEnd;
                e.traveler = tr.id;
                e.station = to;
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

    EngineEvent start;
    start.tick = now_;
    start.type = EngineEventType::TransportStart;
    emit(start);

    // Starters: trigger immediately at tick 0, then depart (BEHAVIORAL §1).
    for (const StationIndex s : graph_->startStations()) {
        const StationId sid = graph_->stations()[s].id;
        if (travelers_.size() >= travelerCap()) {
            ++stats_.droppedTravelers;
            EngineEvent d;
            d.tick = now_;
            d.type = EngineEventType::TravelerDropped;
            d.station = sid;
            emit(d);
            continue;
        }
        const TravelerId id = newTravelerId();
        EngineEvent spawn;
        spawn.tick = now_;
        spawn.type = EngineEventType::TravelerSpawn;
        spawn.traveler = id;
        spawn.station = sid;
        emit(spawn);

        const NoteContext context = trigger(s, id, now_, NoteContext{});
        chooseRoutes(s);
        if (routeScratch_.empty()) {
            EngineEvent end;
            end.tick = now_;
            end.type = EngineEventType::TravelerEnd;
            end.traveler = id;
            end.station = sid;
            end.endReason = EndReason::NoRoute;
            emit(end);
            continue;
        }
        travelers_.push_back(Traveler{id, 0, now_, now_, context});
        depart(travelers_.back(), s, routeScratch_.front(), now_);
        spawnChildren(s, travelers_.size() - 1, 1, now_);
    }
    stats_.travelerHighWater = std::max(stats_.travelerHighWater, travelers_.size());
}

void Sequencer::requestStop() {
    if (transport_ != TransportState::Playing) return;
    stopAt_ = ceilToClock(now_);
    if (*stopAt_ == now_) performStop();
}

void Sequencer::performStop() {
    releaseAllNotes();
    EngineEvent stop;
    stop.tick = now_;
    stop.type = EngineEventType::TransportStop;
    emit(stop);
    stopAt_.reset();
    transport_ = TransportState::Paused;
}

void Sequencer::continuePlayback() {
    if (transport_ != TransportState::Paused) return;
    EngineEvent cont;
    cont.tick = now_;
    cont.type = EngineEventType::TransportContinue;
    emit(cont);
    transport_ = TransportState::Playing;
}

void Sequencer::panic() {
    releaseAllNotes();
    EngineEvent p;
    p.tick = now_;
    p.type = EngineEventType::Panic;
    emit(p);
}

void Sequencer::releaseAllNotes() {
    notes_.releaseAll([this](const ActiveNoteRegistry::Release& r) {
        EngineEvent off;
        off.tick = now_;
        off.type = EngineEventType::NoteOff;
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
    // D21 step 2: Note Offs before Note Ons, so a note ending exactly when the
    // same pitch starts never cuts the new note.
    notes_.releaseDue(t, [this, t](const ActiveNoteRegistry::Release& r) {
        EngineEvent off;
        off.tick = t;
        off.type = EngineEventType::NoteOff;
        off.channel = r.channel;
        off.note = r.note;
        emit(off);
    });

    // D21 step 3: arrivals in ascending TravelerId order.
    dueScratch_.clear();
    for (const auto& tr : travelers_)
        if (tr.deadline == t) dueScratch_.push_back(tr.id);
    std::sort(dueScratch_.begin(), dueScratch_.end());
    for (const TravelerId id : dueScratch_) arrive(id, t);

    stats_.travelerHighWater = std::max(stats_.travelerHighWater, travelers_.size());
}

void Sequencer::arrive(TravelerId id, MusicalTick t) {
    const auto it = std::find_if(travelers_.begin(), travelers_.end(), [id](const Traveler& tr) { return tr.id == id; });
    if (it == travelers_.end()) return;
    const auto index = static_cast<std::size_t>(it - travelers_.begin());

    const StationIndex s = graph_->flowpoints()[travelers_[index].flowpoint].to;
    const StationId sid = graph_->stations()[s].id;

    EngineEvent arriveEvent;
    arriveEvent.tick = t;
    arriveEvent.type = EngineEventType::TravelerArrive;
    arriveEvent.traveler = id;
    arriveEvent.station = sid;
    emit(arriveEvent);

    travelers_[index].context = trigger(s, id, t, travelers_[index].context);
    chooseRoutes(s);

    if (routeScratch_.empty()) { // D24: end of chain
        EngineEvent end;
        end.tick = t;
        end.type = EngineEventType::TravelerEnd;
        end.traveler = id;
        end.station = sid;
        end.endReason = EndReason::NoRoute;
        emit(end);
        travelers_[index] = travelers_.back();
        travelers_.pop_back();
        return;
    }

    depart(travelers_[index], s, routeScratch_.front(), t);
    spawnChildren(s, index, 1, t);
}

namespace {

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

NoteContext Sequencer::trigger(StationIndex s, TravelerId traveler, MusicalTick t, const NoteContext& incoming) {
    const RuntimeStation& st = graph_->stations()[s];
    if (st.eventType != StationEventType::Note) return incoming; // D26: CC/PB never touch note context (SF-007)

    // Resolution always runs, even when silent, so the melodic context and the
    // random streams do not depend on mute state (P2, D19).
    StationValueDraw draw{random_, s, st.id};
    const ResolvedNotes resolved =
        resolveNotes(st.note, ScaleContext{graph_->settings().root, graph_->settings().scale}, incoming, draw);
    if (st.manuallyMuted) return resolved.outgoing;

    for (std::size_t i = 0; i < resolved.count; ++i) { // ascending pitch order (D34)
        const MidiNote pitch = resolved.pitches[i];
        if (!notes_.add(st.channel, pitch, t + st.note.duration)) {
            ++stats_.droppedNotes;
            EngineEvent d;
            d.tick = t;
            d.type = EngineEventType::NoteDropped;
            d.channel = st.channel;
            d.note = pitch;
            d.station = st.id;
            emit(d);
            continue;
        }
        stats_.noteHighWater = std::max(stats_.noteHighWater, notes_.size());

        EngineEvent on;
        on.tick = t;
        on.type = EngineEventType::NoteOn;
        on.channel = st.channel;
        on.note = pitch;
        on.velocity = st.note.velocity;
        on.station = st.id;
        on.traveler = traveler;
        emit(on);
    }
    return resolved.outgoing;
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
        case BranchMode::Random:     // TODO(SF-005): weighted seeded draw (D10, D32)
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
    traveler.flowpoint = route;
    traveler.legStart = t;
    traveler.deadline = t + graph_->flowpoints()[route].duration;

    EngineEvent e;
    e.tick = t;
    e.type = EngineEventType::TravelerDepart;
    e.traveler = traveler.id;
    e.station = graph_->stations()[from].id;
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
            EngineEvent d;
            d.tick = t;
            d.type = EngineEventType::TravelerDropped;
            d.station = graph_->stations()[from].id;
            emit(d);
            continue;
        }
        // Children inherit the parent's context after this Station (D19).
        const NoteContext context = travelers_[parentIndex].context;
        travelers_.push_back(Traveler{newTravelerId(), 0, t, t, context});
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
        const auto& fp = graph_->flowpoints()[tr.flowpoint];
        snap.travelers.push_back(TravelerView{tr.id, fp.id, graph_->stations()[fp.from].id,
                                              graph_->stations()[fp.to].id, tr.legStart, tr.deadline});
    }
    std::sort(snap.travelers.begin(), snap.travelers.end(),
              [](const TravelerView& a, const TravelerView& b) { return a.id < b.id; });
    return snap;
}

} // namespace signalflux::engine
