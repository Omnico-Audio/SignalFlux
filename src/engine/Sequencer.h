// SignalFlux — sequencing engine: Starters, Travelers, tick scheduler (task SF-003).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Canonical time is the integer MusicalTick (960 PPQ). The scheduler is
// event-driven: processUntil() jumps from one due event to the next, so its
// output depends only on the document, the transport actions, and the ticks at
// which edits were applied, never on how often or in what chunk sizes it is
// called. That is what makes UI frame rate irrelevant to musical timing.
//
// Within a tick, work happens in the fixed order of DECISIONS D21:
//   1. edits (applied by setDocument at now())
//   2. Note Offs due at this tick
//   3. arrivals, in ascending TravelerId order
//   (4. logic re-evaluation: SF-006)
//
// A Traveler is either travelling a leg or dwelling at a Station while its
// Repeat group plays (D8). Entering a Station runs, in order: Terminator (D13:
// may end the Traveler), silence decision (manual mute, gate enable, then
// probability, evaluated once per arrival, D7), resolution, Note Ons, then
// either the Repeat group or departure. Mute, gates and probability change
// sound only, never timing or routing (P2).
//
// Logic (D12) is evaluated at the end of a processed tick when its inputs
// changed (D21 step 4) and immediately on external inputs (Switch, MIDI), so
// it keeps running while stopped.
//
// Thread ownership: one Sequencer belongs to one thread (the engine thread).
// No method is thread-safe. After play(), processUntil() performs no
// allocation: Traveler and note storage is reserved at the maximum caps.

#pragma once

#include "engine/ActiveNoteRegistry.h"
#include "engine/EngineEvent.h"
#include "engine/LogicEngine.h"
#include "engine/NoteResolution.h"
#include "engine/Random.h"
#include "engine/RuntimeGraph.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace signalflux::engine {

enum class TransportState : std::uint8_t { Stopped, Playing, Paused };

struct TravelerView {
    TravelerId id;
    bool dwelling = false;       ///< At a Station playing its Repeat group; from == to.
    FlowpointId flowpoint;       ///< Invalid (0) while dwelling.
    StationId from;              ///< Source Station: the sprite's colour (SF-009).
    StationId to;
    MusicalTick legStart;
    MusicalTick deadline;
};

struct EngineStats {
    std::uint64_t droppedTravelers = 0;
    std::uint64_t droppedNotes = 0;
    std::size_t travelerHighWater = 0;
    std::size_t noteHighWater = 0;
};

/// Read-only copy of engine state for the UI. The UI interpolates sprite
/// positions from (legStart, deadline, now); it never feeds anything back.
struct GateView {
    GateId id;
    bool output = false;
};

struct StationRuntimeView {
    StationId id;
    bool enabled = true;              ///< False when its driving gate is false (D12).
    bool active = false;              ///< A note from this Station is sounding.
    std::uint32_t terminatorCount = 0; ///< Arrivals counted since Play (D13).
};

struct EngineSnapshot {
    MusicalTick now;
    TransportState transport = TransportState::Stopped;
    std::vector<TravelerView> travelers;
    std::vector<GateView> gates;           ///< Ascending GateId.
    std::vector<StationRuntimeView> stations; ///< Document order.
    EngineStats stats;
};

class Sequencer {
public:
    explicit Sequencer(IEngineEventSink& sink);

    /// Replaces the document. Takes effect at now(), before any event at now()
    /// (D21 step 1). In-flight Travelers keep their elapsed ticks and get new
    /// deadlines (D3, D4); Travelers on deleted routes end (D5).
    /// If the document is invalid, nothing changes and the report says why.
    ValidationReport setDocument(const ProjectDocument& doc);

    /// Play: reset all runtime state, tick 0, spawn one Starter per Start Station.
    /// `seed` overrides the project's random seed (D10: seedMode NewEachPlay is
    /// implemented by the app passing a fresh seed; tests pass fixed ones).
    void play(std::optional<std::uint64_t> seed = std::nullopt);
    /// Stop at the next MIDI Clock boundary (D14): every 40 ticks.
    void requestStop();
    /// Resume a stopped engine from where it stopped (D14).
    void continuePlayback();
    /// Release every sounding note now. Transport state is unchanged.
    void panic();

    // External logic inputs (D12). Applied at now(), with one logic step, in
    // any transport state. Channels are 1-16 as MidiChannel.
    void setSwitch(SwitchId id, bool on);
    void setMidiNoteHeld(MidiChannel channel, MidiNote note, bool held);
    void setMidiControl(MidiChannel channel, CcNumber controller, CcValue value);

    // MIDI input modulation (D15). Applied at now(); runtime only, never saved.
    /// Global transpose in scale steps, applied to emitted notes only (the
    /// carried melodic context is unchanged).
    void setTranspose(std::int32_t scaleSteps) noexcept { transpose_ = scaleSteps; }
    [[nodiscard]] std::int32_t transpose() const noexcept { return transpose_; }
    void setVelocityOverride(StationId id, std::optional<Velocity> v);
    void setProbabilityOverride(StationId id, std::optional<ProbabilityPercent> p);

    /// Processes every event with tick < horizon, then sets now() = horizon.
    /// Does nothing unless playing. `horizon` is on the engine's own tick
    /// timeline; the host derives it from its clock (SF-007).
    void processUntil(MusicalTick horizon);

    [[nodiscard]] MusicalTick now() const noexcept { return now_; }
    [[nodiscard]] TransportState transport() const noexcept { return transport_; }
    [[nodiscard]] std::optional<MusicalTick> pendingStop() const noexcept { return stopAt_; }
    [[nodiscard]] const EngineStats& stats() const noexcept { return stats_; }
    /// Seed used by the current (or last) Play (D29 seed history).
    [[nodiscard]] std::uint64_t currentSeed() const noexcept { return random_.seed(); }
    [[nodiscard]] EngineSnapshot snapshot() const;
    /// Fills `out`, reusing its storage: after warm-up this does not allocate,
    /// so a real-time host can publish snapshots from the engine thread.
    void snapshotInto(EngineSnapshot& out) const;

private:
    enum class Phase : std::uint8_t { Travelling, Dwelling };

    struct Traveler {
        TravelerId id;
        Phase phase = Phase::Travelling;
        FlowpointIndex flowpoint = 0;          ///< Travelling: current leg.
        StationIndex station = 0;              ///< Dwelling: current Station.
        MusicalTick legStart;                  ///< Travelling: leg start. Dwelling: arrival.
        MusicalTick deadline;                  ///< Next arrival or next Repeat trigger.
        NoteContext context;                   ///< Carried signal data (D19, D39).
        // Repeat group (D8), valid while dwelling.
        ResolvedNotes repeatNotes;
        bool silent = false;
        std::uint32_t repeatsLeft = 0;
        MusicalTick repeatInterval;
    };

    void emit(EngineEvent e) noexcept { sink_.onEngineEvent(e); }
    void processTick(MusicalTick t);
    void enterStation(std::size_t index, StationIndex s, MusicalTick t);
    void repeatTrigger(std::size_t index, MusicalTick t);
    void departOrEnd(std::size_t index, StationIndex s, MusicalTick t);
    [[nodiscard]] bool isSilent(StationIndex s) noexcept;
    void emitNotes(StationIndex s, const ResolvedNotes& notes, TravelerId traveler, MusicalTick t);
    void startNote(StationId station, MidiChannel ch, MidiNote pitch, Velocity v, TravelerId traveler, MusicalTick at,
                   MusicalTick duration);
    void emitPendingNotes(MusicalTick t);
    [[nodiscard]] MusicalTick swung(MusicalTick t) const noexcept;
    void chooseRoutes(StationIndex s);
    void depart(Traveler& traveler, StationIndex from, FlowpointIndex route, MusicalTick t);
    void spawnChildren(StationIndex from, std::size_t parentIndex, std::size_t firstRoute, MusicalTick t);
    void endTraveler(std::size_t index, StationId at, EndReason why, MusicalTick t);
    void performStop();
    void releaseAllNotes();
    void silencePendingRepeats() noexcept;
    bool runLogicStep() noexcept;
    void emitControl(StationIndex s, MusicalTick t);
    void stepRamps(MusicalTick t);
    void emitControlValue(MidiChannel ch, bool pitchBend, CcNumber cc, std::int32_t value, StationId st, MusicalTick t);
    void noteReleased(StationId station) noexcept;
    [[nodiscard]] std::optional<MusicalTick> nextEventTick() const noexcept;
    [[nodiscard]] std::size_t travelerCap() const noexcept;
    [[nodiscard]] TravelerId newTravelerId() noexcept { return TravelerId{nextTravelerId_++}; }

    IEngineEventSink& sink_;
    std::optional<RuntimeGraph> graph_;
    TransportState transport_ = TransportState::Stopped;
    MusicalTick now_;
    std::optional<MusicalTick> stopAt_;

    std::vector<Traveler> travelers_;          // active Travelers, unordered
    std::vector<std::uint32_t> roundRobin_;    // per Station index: next outgoing slot (D6)
    std::vector<FlowpointIndex> routeScratch_; // chosen routes for the current departure
    std::vector<TravelerId> dueScratch_;       // arrivals at the current tick
    ActiveNoteRegistry notes_;
    RandomStreams random_;
    LogicEngine logic_;
    MidiInputState midiIn_;
    std::vector<std::int32_t> stationSounding_;     // per Station index: sounding instances
    std::vector<std::uint8_t> stationActive_;       // scratch for logic steps
    std::vector<std::uint32_t> terminatorCount_;    // per Station index (D13)
    bool logicDirty_ = true;

    // CC / Pitch Bend ramps (D26): bounded, last writer per (channel, kind, controller) wins.
    struct Ramp {
        bool active = false;
        MidiChannel channel;
        bool pitchBend = false;
        CcNumber controller;
        std::int32_t from = 0, to = 0, last = 0;
        MusicalTick start, end, next;
        StationId station;
    };
    static constexpr std::size_t kMaxRamps = 64;
    static constexpr std::int64_t kRampStepTicks = 10; // D26: 96 steps per quarter
    std::array<Ramp, kMaxRamps> ramps_{};
    std::array<std::array<std::int16_t, 128>, 16> ccValue_{};  // last value sent per channel/controller
    std::array<std::int16_t, 16> pitchBendValue_{};            // last Pitch Bend per channel (0 = centre)

    // Delayed Note Ons (D36 swing and humanize): bounded, sorted by (tick, sequence).
    struct PendingNote {
        MusicalTick at;
        std::uint64_t sequence = 0;
        StationId station;
        MidiChannel channel;
        MidiNote pitch;
        Velocity velocity;
        TravelerId traveler;
        MusicalTick duration;
    };
    static constexpr std::size_t kMaxPending = 512;
    std::array<PendingNote, kMaxPending> pending_{};
    std::size_t pendingCount_ = 0;
    std::uint64_t pendingSequence_ = 0;

    std::int32_t transpose_ = 0;
    std::vector<std::pair<StationId, Velocity>> velocityOverrides_;
    std::vector<std::pair<StationId, ProbabilityPercent>> probabilityOverrides_;
    std::uint32_t nextTravelerId_ = 1;
    EngineStats stats_;
};

} // namespace signalflux::engine
