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
    velocityOverrides_.reserve(64);
    probabilityOverrides_.reserve(64);
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
        std::vector<std::int32_t> sounding(compiled->stations().size(), 0);
        std::vector<std::uint32_t> terminators(compiled->stations().size(), 0);
        for (std::size_t i = 0; i < newToOld.size(); ++i)
            if (newToOld[i] >= 0) {
                sounding[i] = stationSounding_[static_cast<std::size_t>(newToOld[i])];
                terminators[i] = terminatorCount_[static_cast<std::size_t>(newToOld[i])];
            }
        stationSounding_ = std::move(sounding);
        terminatorCount_ = std::move(terminators);
    } else {
        roundRobin_.assign(compiled->stations().size(), 0);
        random_.reset(compiled->settings().randomSeed, compiled->stations().size());
        stationSounding_.assign(compiled->stations().size(), 0);
        terminatorCount_.assign(compiled->stations().size(), 0);
    }
    stationActive_.assign(compiled->stations().size(), 0);

    std::size_t maxOut = 0;
    for (const auto& s : compiled->stations()) maxOut = std::max(maxOut, s.outgoing.size());
    routeScratch_.reserve(maxOut);

    graph_ = std::move(compiled);
    notes_.setCapacity(graph_->settings().activeNoteCap);
    logic_.compile(doc.plots.front(), *graph_);
    logicDirty_ = runLogicStep();
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
    std::fill(stationSounding_.begin(), stationSounding_.end(), 0);
    std::fill(terminatorCount_.begin(), terminatorCount_.end(), 0u); // D13: reset on Play
    logicDirty_ = true;
    ramps_ = {};
    pendingCount_ = 0;
    for (auto& ch : ccValue_) ch.fill(0);
    pitchBendValue_.fill(0);
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
    if (logicDirty_) logicDirty_ = runLogicStep(); // D21 step 4 for tick 0
}

void Sequencer::requestStop() {
    if (transport_ != TransportState::Playing) return;
    stopAt_ = ceilToClock(now_);
    if (*stopAt_ == now_) performStop();
}

void Sequencer::performStop() {
    releaseAllNotes();
    silencePendingRepeats();
    ramps_ = {};       // D26: Stop freezes ramps where they are
    pendingCount_ = 0; // delayed notes not yet started are dropped: nothing to release
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
    ramps_ = {};
    pendingCount_ = 0;
    pitchBendValue_.fill(0); // the Panic message set recentres Pitch Bend (D14)
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
    if (std::any_of(stationSounding_.begin(), stationSounding_.end(), [](std::int32_t c) { return c != 0; })) {
        std::fill(stationSounding_.begin(), stationSounding_.end(), 0);
        logicDirty_ = runLogicStep(); // Station Active signals all dropped
    }
}

void Sequencer::noteReleased(StationId station) noexcept {
    if (!graph_) return;
    if (const auto s = graph_->stationIndex(station); s && stationSounding_[*s] > 0)
        if (--stationSounding_[*s] == 0) logicDirty_ = true;
}

bool Sequencer::runLogicStep() noexcept {
    for (std::size_t i = 0; i < stationSounding_.size(); ++i) stationActive_[i] = stationSounding_[i] > 0 ? 1 : 0;
    return logic_.step(stationActive_, midiIn_);
}

void Sequencer::setSwitch(SwitchId id, bool on) {
    if (logic_.setSwitch(id, on)) logicDirty_ = runLogicStep();
}

void Sequencer::setMidiNoteHeld(MidiChannel channel, MidiNote note, bool held) {
    midiIn_.noteHeld[static_cast<std::size_t>(channel.value() - 1)][static_cast<std::size_t>(note.value())] = held ? 1 : 0;
    logicDirty_ = runLogicStep();
}

void Sequencer::setMidiControl(MidiChannel channel, CcNumber controller, CcValue value) {
    midiIn_.ccValue[static_cast<std::size_t>(channel.value() - 1)][static_cast<std::size_t>(controller.value())] =
        static_cast<std::uint8_t>(value.value());
    logicDirty_ = runLogicStep();
}

// ---------------------------------------------------------------------------
// Scheduling
// ---------------------------------------------------------------------------

std::optional<MusicalTick> Sequencer::nextEventTick() const noexcept {
    std::optional<MusicalTick> next = notes_.nextDue();
    for (const auto& tr : travelers_)
        if (!next || tr.deadline < *next) next = tr.deadline;
    for (const auto& r : ramps_)
        if (r.active && (!next || r.next < *next)) next = r.next;
    if (pendingCount_ > 0 && (!next || pending_[0].at < *next)) next = pending_[0].at;
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
        noteReleased(r.station);
        if (!r.sendNoteOff) return; // D22: another instance of this pitch still sounds
        auto off = makeEvent(EngineEventType::NoteOff, t);
        off.channel = r.channel;
        off.note = r.note;
        emit(off);
    });
    stepRamps(t);         // low-priority controller updates, after Note Offs
    emitPendingNotes(t);  // swung / humanized Note Ons scheduled earlier (D36)

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

    // D21 step 4: logic sees this tick's signals from the next tick on.
    if (logicDirty_) logicDirty_ = runLogicStep();
}

bool Sequencer::isSilent(StationIndex s) noexcept {
    const RuntimeStation& st = graph_->stations()[s];
    if (st.manuallyMuted || !logic_.stationEnabled(s)) return true;
    // D7: probability is evaluated once per arrival; 0 and 100 consume no draw.
    int p = st.probability.value();
    for (const auto& [id, value] : probabilityOverrides_)
        if (id == st.id) p = value.value(); // D15 MIDI modulation
    if (p == 0) return true;
    if (p == 100) return false;
    return random_.below(s, st.id, RandomPurpose::Probability, 100) >= static_cast<std::uint64_t>(p);
}

void Sequencer::enterStation(std::size_t index, StationIndex s, MusicalTick t) {
    const RuntimeStation& st = graph_->stations()[s];

    // D13: a Terminator counts every Traveler entering the Station, silent or
    // not. Arrivals beyond the limit end here: no trigger, no departure.
    if (st.terminatorLimit) {
        if (++terminatorCount_[s] > *st.terminatorLimit) {
            endTraveler(index, st.id, EndReason::Terminated, t);
            return;
        }
    }

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
    else if (!silent) {
        emitControl(s, t); // D26: CC / Pitch Bend Stations never touch the note context
    }

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
    if (!tr.silent) {
        if (graph_->stations()[s].eventType == StationEventType::Note) emitNotes(s, tr.repeatNotes, tr.id, t);
        else emitControl(s, t);
    }
    if (--tr.repeatsLeft > 0) {
        tr.deadline = t + tr.repeatInterval;
        return;
    }
    departOrEnd(index, s, t); // D8: departure coincides with the final trigger
}

/// D36 swing: an event exactly on an off-beat of the swing grid is delayed so
/// the off-beat lands at swing% of the pair. 50% = straight. Integer ticks.
MusicalTick Sequencer::swung(MusicalTick t) const noexcept {
    const auto& st = graph_->settings();
    if (st.swingPercent <= 50) return t;
    const std::int64_t grid = st.swingGrid == SwingGrid::Eighths ? kTicksPerQuarter / 2 : kTicksPerQuarter / 4;
    if (t.value() % (2 * grid) != grid) return t;
    return t + MusicalTick{grid * (2 * st.swingPercent - 100) / 100};
}

void Sequencer::emitNotes(StationIndex s, const ResolvedNotes& resolved, TravelerId traveler, MusicalTick t) {
    const RuntimeStation& st = graph_->stations()[s];
    int velocity = st.note.velocity.value();
    for (const auto& [id, value] : velocityOverrides_)
        if (id == st.id) velocity = value.value(); // D15 MIDI modulation

    // D36 humanize: one draw per trigger, shared by chord tones. No draw when off,
    // so projects without humanize keep identical random streams.
    if (st.humanize.velocityRange > 0)
        velocity = std::clamp(velocity + static_cast<int>(random_.between(s, st.id, RandomPurpose::HumanizeVelocity,
                                                                           -st.humanize.velocityRange, st.humanize.velocityRange)),
                              1, 127);
    MusicalTick at = swung(t);
    if (st.humanize.maxDelay.value() > 0)
        at = at + MusicalTick{random_.between(s, st.id, RandomPurpose::HumanizeTiming, 0, st.humanize.maxDelay.value())};

    const ScaleContext scale{graph_->settings().root, graph_->settings().scale};
    for (std::size_t i = 0; i < resolved.count; ++i) { // ascending pitch order (D34)
        const MidiNote pitch = transpose_ == 0
            ? resolved.pitches[i]
            : *MidiNote::make(foldToMidiRange(stepInScale(resolved.pitches[i].value(), transpose_, scale)));
        const auto v = *Velocity::make(velocity);
        if (at == t || pendingCount_ == kMaxPending) { // overflow policy: play on time
            startNote(st.id, st.channel, pitch, v, traveler, t, st.note.duration);
            continue;
        }
        // Insert keeping (tick, sequence) order; bounded, no allocation.
        PendingNote p{at, pendingSequence_++, st.id, st.channel, pitch, v, traveler, st.note.duration};
        std::size_t k = pendingCount_++;
        while (k > 0 && pending_[k - 1].at > p.at) {
            pending_[k] = pending_[k - 1];
            --k;
        }
        pending_[k] = p;
    }
}

void Sequencer::startNote(StationId station, MidiChannel ch, MidiNote pitch, Velocity v, TravelerId traveler, MusicalTick at,
                          MusicalTick duration) {
    if (!notes_.add(ch, pitch, at + duration, station)) {
        ++stats_.droppedNotes;
        auto d = makeEvent(EngineEventType::NoteDropped, at, {}, station);
        d.channel = ch;
        d.note = pitch;
        emit(d);
        return;
    }
    stats_.noteHighWater = std::max(stats_.noteHighWater, notes_.size());
    if (const auto s = graph_->stationIndex(station))
        if (stationSounding_[*s]++ == 0) logicDirty_ = true;
    auto on = makeEvent(EngineEventType::NoteOn, at, traveler, station);
    on.channel = ch;
    on.note = pitch;
    on.velocity = v;
    emit(on);
}

void Sequencer::emitPendingNotes(MusicalTick t) {
    std::size_t due = 0;
    while (due < pendingCount_ && pending_[due].at <= t) ++due;
    for (std::size_t i = 0; i < due; ++i) {
        const auto& p = pending_[i];
        startNote(p.station, p.channel, p.pitch, p.velocity, p.traveler, t, p.duration);
    }
    for (std::size_t i = due; i < pendingCount_; ++i) pending_[i - due] = pending_[i];
    pendingCount_ -= due;
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

// ---------------------------------------------------------------------------
// CC and Pitch Bend Stations (D26)
// ---------------------------------------------------------------------------

void Sequencer::emitControlValue(MidiChannel ch, bool pitchBend, CcNumber cc, std::int32_t value, StationId st,
                                 MusicalTick t) {
    const auto c = static_cast<std::size_t>(ch.value() - 1);
    auto e = makeEvent(pitchBend ? EngineEventType::PitchBend : EngineEventType::ControlChange, t, {}, st);
    e.channel = ch;
    e.controller = cc;
    e.value = value;
    if (pitchBend) pitchBendValue_[c] = static_cast<std::int16_t>(value);
    else ccValue_[c][static_cast<std::size_t>(cc.value())] = static_cast<std::int16_t>(value);
    emit(e);
}

void Sequencer::emitControl(StationIndex s, MusicalTick t) {
    const RuntimeStation& st = graph_->stations()[s];
    const bool pb = st.eventType == StationEventType::PitchBend;
    const auto c = static_cast<std::size_t>(st.channel.value() - 1);
    const CcNumber cc = st.controlChange.controller;
    const std::int32_t target = pb ? st.pitchBend.target.value() : st.controlChange.target.value();
    const bool ramp = pb ? st.pitchBend.ramp : st.controlChange.ramp;
    const std::int32_t current = pb ? pitchBendValue_[c] : ccValue_[c][static_cast<std::size_t>(cc.value())];

    // A newer value or ramp for the same destination replaces any running ramp.
    Ramp* slot = nullptr;
    for (auto& r : ramps_)
        if (r.active && r.channel == st.channel && r.pitchBend == pb && (pb || r.controller == cc)) {
            r.active = false;
            slot = &r;
        }

    if (!ramp || current == target) {
        if (!ramp) emitControlValue(st.channel, pb, cc, target, st.id, t);
        return;
    }
    if (slot == nullptr)
        for (auto& r : ramps_)
            if (!r.active) {
                slot = &r;
                break;
            }
    if (slot == nullptr) { // overflow policy: jump straight to the target
        emitControlValue(st.channel, pb, cc, target, st.id, t);
        return;
    }
    *slot = Ramp{true, st.channel, pb, cc, current, target, current,
                 t, t + st.note.duration, t + MusicalTick{std::min(kRampStepTicks, st.note.duration.value())}, st.id};
}

void Sequencer::stepRamps(MusicalTick t) {
    for (auto& r : ramps_) {
        if (!r.active || r.next != t) continue;
        std::int32_t v = r.to;
        if (t < r.end) {
            const auto span = (r.end - r.start).value();
            const auto done = (t - r.start).value();
            v = r.from + static_cast<std::int32_t>(static_cast<std::int64_t>(r.to - r.from) * done / span);
        }
        if (v != r.last) {
            r.last = v;
            emitControlValue(r.channel, r.pitchBend, r.controller, v, r.station, t);
        }
        if (t >= r.end) {
            r.active = false;
            continue;
        }
        r.next = std::min(r.end, t + MusicalTick{kRampStepTicks});
    }
}

// ---------------------------------------------------------------------------
// MIDI input modulation (D15)
// ---------------------------------------------------------------------------

void Sequencer::setVelocityOverride(StationId id, std::optional<Velocity> v) {
    std::erase_if(velocityOverrides_, [id](const auto& e) { return e.first == id; });
    if (v) velocityOverrides_.emplace_back(id, *v);
}

void Sequencer::setProbabilityOverride(StationId id, std::optional<ProbabilityPercent> p) {
    std::erase_if(probabilityOverrides_, [id](const auto& e) { return e.first == id; });
    if (p) probabilityOverrides_.emplace_back(id, *p);
}

EngineSnapshot Sequencer::snapshot() const {
    EngineSnapshot snap;
    snapshotInto(snap);
    return snap;
}

void Sequencer::snapshotInto(EngineSnapshot& snap) const {
    snap.now = now_;
    snap.transport = transport_;
    snap.stats = stats_;
    snap.travelers.clear();
    snap.gates.clear();
    snap.stations.clear();
    if (!graph_) return;
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
    for (std::size_t i = 0; i < logic_.gateCount(); ++i) snap.gates.push_back(GateView{logic_.gateId(i), logic_.output(i)});
    for (std::size_t i = 0; i < graph_->stations().size(); ++i)
        snap.stations.push_back(StationRuntimeView{graph_->stations()[i].id,
                                                   logic_.stationEnabled(static_cast<StationIndex>(i)),
                                                   stationSounding_[i] > 0, terminatorCount_[i]});
}

} // namespace signalflux::engine
