// SignalFlux — events produced by the sequencing engine (task SF-003).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// The engine emits one ordered stream of events. MIDI output, the optional
// synth, the Traveler renderer, Capture, and the golden-log tests all consume
// this same stream (README: "MIDI output and the optional synth consume the
// same generated musical events").

#pragma once

#include "domain/Types.h"

#include <cstdint>
#include <string>

namespace signalflux::engine {

enum class EngineEventType : std::uint8_t {
    TransportStart,
    TransportStop,
    TransportContinue,
    Panic,
    TravelerSpawn,     ///< A Starter created on Play.
    TravelerArrive,
    TravelerDepart,    ///< Leg started; `flowpoint` and `deadline` are set.
    TravelerRepeat,    ///< A Repeat trigger while dwelling at a Station (D8).
    TravelerEnd,       ///< Chain ended: no outgoing route (D24) or route deleted (D5).
    TravelerDropped,   ///< Not spawned: Traveler cap reached (D23).
    NoteOn,
    NoteOff,
    NoteDropped,       ///< Not sent: active-note cap reached (D23).
    ControlChange,     ///< D26: `channel`, `controller`, `value` 0–127.
    PitchBend,         ///< D26: `channel`, `value` −8192…8191.
};

enum class EndReason : std::uint8_t { None, NoRoute, RouteDeleted, StationDeleted, Terminated };

struct EngineEvent {
    MusicalTick tick;
    EngineEventType type = EngineEventType::NoteOn;
    TravelerId traveler;
    StationId station;
    FlowpointId flowpoint;
    MusicalTick deadline;          ///< TravelerDepart only.
    MidiChannel channel;           ///< Note events only.
    MidiNote note;                 ///< Note events only.
    Velocity velocity;             ///< NoteOn only.
    CcNumber controller;           ///< ControlChange only.
    std::int32_t value = 0;        ///< ControlChange / PitchBend.
    EndReason endReason = EndReason::None;
};

/// Receives engine events in emission order. Called on the engine thread.
/// Implementations used on deadline-critical paths must not block or allocate.
class IEngineEventSink {
public:
    virtual ~IEngineEventSink() = default;
    virtual void onEngineEvent(const EngineEvent& event) noexcept = 0;
};

/// Canonical, platform-independent text form of one event (D25). One line, no
/// trailing newline. Golden logs are sequences of these lines joined by '\n'.
[[nodiscard]] std::string toCanonicalString(const EngineEvent& event);

} // namespace signalflux::engine
