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
// Repeat group plays (D8). Entering a Station runs, in order: (SF-006:
// Terminator), silence decision (manual mute, gates, probability: evaluated
// once per arrival, D7), resolution, Note Ons, then either the Repeat group or
// departure. Mute and probability change sound only, never timing or routing (P2).
//
// Thread ownership: one Sequencer belongs to one thread (the engine thread).
// No method is thread-safe. After play(), processUntil() performs no
// allocation: Traveler and note storage is reserved at the maximum caps.

#pragma once

#include "engine/ActiveNoteRegistry.h"
#include "engine/EngineEvent.h"
#include "engine/NoteResolution.h"
#include "engine/Random.h"
#include "engine/RuntimeGraph.h"

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
struct EngineSnapshot {
    MusicalTick now;
    TransportState transport = TransportState::Stopped;
    std::vector<TravelerView> travelers;
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
    void chooseRoutes(StationIndex s);
    void depart(Traveler& traveler, StationIndex from, FlowpointIndex route, MusicalTick t);
    void spawnChildren(StationIndex from, std::size_t parentIndex, std::size_t firstRoute, MusicalTick t);
    void endTraveler(std::size_t index, StationId at, EndReason why, MusicalTick t);
    void performStop();
    void releaseAllNotes();
    void silencePendingRepeats() noexcept;
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
    std::uint32_t nextTravelerId_ = 1;
    EngineStats stats_;
};

} // namespace signalflux::engine
