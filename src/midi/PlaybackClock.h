// SignalFlux — tick <-> time conversion at the output boundary (ARCHITECTURE §6; D25).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Exact integer arithmetic: ticks are derived from an anchor (tick, time) and
// the tempo in milli-BPM, with 128-bit intermediate products, so there is no
// floating point and no long-term drift. A tempo change re-anchors at the
// current tick, so no tick is skipped or repeated.
//
// Time unit: microseconds on a monotonic clock supplied by the caller.

#pragma once

#include "domain/Types.h"

#include <cstdint>

namespace signalflux::midi {

/// floor(a * b / c) without overflow for any 64-bit a, b and c > 0, provided the
/// result fits in 64 bits. Portable (no __int128).
[[nodiscard]] std::uint64_t mulDiv(std::uint64_t a, std::uint64_t b, std::uint64_t c) noexcept;

class PlaybackClock {
public:
    /// Starts counting `startTick` at time `nowUs`.
    void start(MusicalTick startTick, std::int64_t nowUs, MilliBpm tempo) noexcept {
        anchorTick_ = startTick;
        anchorUs_ = nowUs;
        tempo_ = tempo;
    }

    /// Musical tick reached at `nowUs` (floor). Never earlier than the anchor.
    [[nodiscard]] MusicalTick tickAt(std::int64_t nowUs) const noexcept {
        if (nowUs <= anchorUs_) return anchorTick_;
        const auto elapsed = static_cast<std::uint64_t>(nowUs - anchorUs_);
        // ticks = elapsedUs * milliBpm * PPQ / (60 s * 1e6 us/s * 1000 milli)
        const auto ticks = mulDiv(elapsed, static_cast<std::uint64_t>(tempo_.value()) * kTicksPerQuarter, kDivisor);
        return anchorTick_ + MusicalTick{static_cast<std::int64_t>(ticks)};
    }

    /// Earliest time at which `tick` is reached (ceil). For ticks before the anchor, the anchor time.
    [[nodiscard]] std::int64_t timeOf(MusicalTick tick) const noexcept {
        if (tick <= anchorTick_) return anchorUs_;
        const auto dt = static_cast<std::uint64_t>((tick - anchorTick_).value());
        const auto perTick = static_cast<std::uint64_t>(tempo_.value()) * kTicksPerQuarter;
        const auto us = mulDiv(dt, kDivisor, perTick) + (mulDivRemainder(dt, kDivisor, perTick) ? 1u : 0u);
        return anchorUs_ + static_cast<std::int64_t>(us);
    }

    /// Changes tempo without a discontinuity: re-anchors at the current tick.
    void setTempo(MilliBpm tempo, std::int64_t nowUs) noexcept {
        const auto t = tickAt(nowUs);
        anchorUs_ = timeOf(t);
        anchorTick_ = t;
        tempo_ = tempo;
    }

    [[nodiscard]] MilliBpm tempo() const noexcept { return tempo_; }

private:
    static constexpr std::uint64_t kDivisor = 60ull * 1'000'000ull * 1000ull;
    static bool mulDivRemainder(std::uint64_t a, std::uint64_t b, std::uint64_t c) noexcept;

    MusicalTick anchorTick_;
    std::int64_t anchorUs_ = 0;
    MilliBpm tempo_;
};

/// MIDI Clock pulses (24 per quarter, D14) are due at every multiple of
/// kTicksPerMidiClock. Returns the first pulse tick at or after `tick`.
[[nodiscard]] constexpr MusicalTick nextClockPulse(MusicalTick tick) noexcept {
    const auto r = tick.value() % kTicksPerMidiClock;
    return MusicalTick{r == 0 ? tick.value() : tick.value() + kTicksPerMidiClock - r};
}

} // namespace signalflux::midi
