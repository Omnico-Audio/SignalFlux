// SignalFlux — MIDI 1.0 wire encoding (task SF-007; ARCHITECTURE §8–9; DECISIONS D14).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Pure functions, no devices: engine events become byte messages here, so the
// protocol can be unit-tested on every platform. Device adapters only send.

#pragma once

#include "domain/Types.h"
#include "engine/EngineEvent.h"

#include <array>
#include <cstdint>

namespace signalflux::midi {

struct MidiMessage {
    std::array<std::uint8_t, 3> bytes{};
    std::uint8_t size = 0;
    friend bool operator==(const MidiMessage&, const MidiMessage&) = default;
};

[[nodiscard]] MidiMessage noteOn(MidiChannel ch, MidiNote note, Velocity v) noexcept;
/// Note Off as status 0x8n with release velocity 0.
[[nodiscard]] MidiMessage noteOff(MidiChannel ch, MidiNote note) noexcept;
[[nodiscard]] MidiMessage controlChange(MidiChannel ch, std::uint8_t controller, std::uint8_t value) noexcept;
/// Pitch Bend from a signed value −8192…8191 (centre 0), sent as 14-bit LSB/MSB.
[[nodiscard]] MidiMessage pitchBend(MidiChannel ch, PitchBendValue v) noexcept;
[[nodiscard]] MidiMessage clockPulse() noexcept;      // 0xF8
[[nodiscard]] MidiMessage start() noexcept;           // 0xFA
[[nodiscard]] MidiMessage continuePlayback() noexcept; // 0xFB
[[nodiscard]] MidiMessage stop() noexcept;            // 0xFC
/// Song Position Pointer in MIDI beats (sixteenth notes), 0–16383.
[[nodiscard]] MidiMessage songPosition(std::uint16_t sixteenths) noexcept;

inline constexpr std::uint8_t kAllSoundOff = 120;
inline constexpr std::uint8_t kAllNotesOff = 123;

/// Maximum messages one engine event can expand to (Panic: 3 per channel).
inline constexpr std::size_t kMaxMessagesPerEvent = 48;

/// Encodes one engine event. Writes up to kMaxMessagesPerEvent messages and
/// returns how many. Traveler events produce none. With `songPositionBeforeStart`,
/// Start is preceded by SPP 0 (D14). Panic expands to All Notes Off, All Sound
/// Off and a centred Pitch Bend on all 16 channels (the engine has already sent
/// Note Offs for every tracked note).
std::size_t encode(const engine::EngineEvent& e, bool songPositionBeforeStart,
                   std::array<MidiMessage, kMaxMessagesPerEvent>& out) noexcept;

} // namespace signalflux::midi
