// SignalFlux — MIDI 1.0 wire encoding.
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "midi/MidiBytes.h"

namespace signalflux::midi {

namespace {

constexpr std::uint8_t status(std::uint8_t kind, MidiChannel ch) noexcept {
    return static_cast<std::uint8_t>(kind | static_cast<std::uint8_t>(ch.value() - 1));
}

constexpr MidiMessage msg(std::uint8_t a) noexcept { return MidiMessage{{a, 0, 0}, 1}; }
constexpr MidiMessage msg(std::uint8_t a, std::uint8_t b, std::uint8_t c) noexcept { return MidiMessage{{a, b, c}, 3}; }

} // namespace

MidiMessage noteOn(MidiChannel ch, MidiNote note, Velocity v) noexcept {
    return msg(status(0x90, ch), static_cast<std::uint8_t>(note.value()), static_cast<std::uint8_t>(v.value()));
}

MidiMessage noteOff(MidiChannel ch, MidiNote note) noexcept {
    return msg(status(0x80, ch), static_cast<std::uint8_t>(note.value()), 0);
}

MidiMessage controlChange(MidiChannel ch, std::uint8_t controller, std::uint8_t value) noexcept {
    return msg(status(0xB0, ch), static_cast<std::uint8_t>(controller & 0x7F), static_cast<std::uint8_t>(value & 0x7F));
}

MidiMessage pitchBend(MidiChannel ch, PitchBendValue v) noexcept {
    const auto raw = static_cast<std::uint16_t>(v.value() + 8192); // 0…16383, centre 8192
    return msg(status(0xE0, ch), static_cast<std::uint8_t>(raw & 0x7F), static_cast<std::uint8_t>((raw >> 7) & 0x7F));
}

MidiMessage clockPulse() noexcept { return msg(0xF8); }
MidiMessage start() noexcept { return msg(0xFA); }
MidiMessage continuePlayback() noexcept { return msg(0xFB); }
MidiMessage stop() noexcept { return msg(0xFC); }

MidiMessage songPosition(std::uint16_t sixteenths) noexcept {
    const auto v = static_cast<std::uint16_t>(sixteenths & 0x3FFF);
    return msg(0xF2, static_cast<std::uint8_t>(v & 0x7F), static_cast<std::uint8_t>((v >> 7) & 0x7F));
}

std::size_t encode(const engine::EngineEvent& e, bool songPositionBeforeStart,
                   std::array<MidiMessage, kMaxMessagesPerEvent>& out) noexcept {
    using T = engine::EngineEventType;
    switch (e.type) {
        case T::NoteOn: out[0] = noteOn(e.channel, e.note, e.velocity); return 1;
        case T::NoteOff: out[0] = noteOff(e.channel, e.note); return 1;
        case T::ControlChange:
            out[0] = controlChange(e.channel, static_cast<std::uint8_t>(e.controller.value()),
                                   static_cast<std::uint8_t>(e.value));
            return 1;
        case T::PitchBend: out[0] = pitchBend(e.channel, *PitchBendValue::make(e.value)); return 1;
        case T::TransportStart:
            if (songPositionBeforeStart) {
                out[0] = songPosition(0);
                out[1] = start();
                return 2;
            }
            out[0] = start();
            return 1;
        case T::TransportStop: out[0] = stop(); return 1;
        case T::TransportContinue: out[0] = continuePlayback(); return 1;
        case T::Panic: {
            std::size_t n = 0;
            for (int c = 1; c <= 16; ++c) {
                const auto ch = *MidiChannel::make(c);
                out[n++] = controlChange(ch, kAllNotesOff, 0);
                out[n++] = controlChange(ch, kAllSoundOff, 0);
                out[n++] = pitchBend(ch, PitchBendValue{});
            }
            return n;
        }
        case T::TravelerSpawn:
        case T::TravelerArrive:
        case T::TravelerDepart:
        case T::TravelerRepeat:
        case T::TravelerEnd:
        case T::TravelerDropped:
        case T::NoteDropped:
            return 0;
    }
    return 0;
}

} // namespace signalflux::midi
