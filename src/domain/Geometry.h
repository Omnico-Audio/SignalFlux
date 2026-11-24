// SignalFlux — route geometry and leg timing (DECISIONS D2, D33, P1).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// This is the single source of truth for "how long is a leg". The engine
// scheduler, the Traveler renderer, and beat ticks (D31) all call it, so
// timing and visuals cannot disagree.

#pragma once

#include "domain/Types.h"

#include <cstdint>
#include <optional>

namespace signalflux {

/// Manhattan length in cells of the orthogonal route between two cells.
/// Elbow direction does not affect length (D2).
[[nodiscard]] constexpr std::int64_t manhattanLength(GridCoordinate a, GridCoordinate b) noexcept {
    const std::int64_t dx = static_cast<std::int64_t>(a.x) - b.x;
    const std::int64_t dy = static_cast<std::int64_t>(a.y) - b.y;
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
}

/// A rate is usable at a spacing only if every integer route length yields an
/// integer duration: spacing × den must be divisible by num (D33).
[[nodiscard]] constexpr bool isRateExact(GridSpacing spacing, RouteRate rate) noexcept {
    return (spacing.ticks() * rate.denominator()) % rate.numerator() == 0;
}

/// leg_duration_ticks = length_cells × grid_spacing_ticks × den / num.
/// Returns nullopt if the result would not be an exact integer or the length is negative.
[[nodiscard]] constexpr std::optional<MusicalTick> legDuration(std::int64_t lengthCells,
                                                               GridSpacing spacing,
                                                               RouteRate rate = RouteRate{}) noexcept {
    if (lengthCells < 0) return std::nullopt;
    const std::int64_t scaled = lengthCells * spacing.ticks() * rate.denominator();
    if (scaled % rate.numerator() != 0) return std::nullopt;
    return MusicalTick{scaled / rate.numerator()};
}

} // namespace signalflux
