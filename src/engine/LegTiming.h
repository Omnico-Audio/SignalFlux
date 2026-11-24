// SignalFlux — in-flight leg timing policy (DECISIONS D3, D4, P1).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Isolated so the "what happens when geometry changes mid-flight" rule can be
// tested on its own (SF-003 acceptance) and swapped by a future decision
// without touching the scheduler.

#pragma once

#include "domain/Types.h"

namespace signalflux::engine {

/// D3/D4: when a leg's length changes while a Traveler is in flight (either
/// endpoint moved, grid spacing or route rate changed), the ticks already
/// travelled are kept and the deadline is recomputed from the new length:
///
///     deadline = legStart + newDuration
///
/// If that deadline is not after `now` (the tick at which the edit is applied),
/// the Traveler arrives at `now`. Arrivals are never scheduled in the past.
[[nodiscard]] constexpr MusicalTick recomputeDeadline(MusicalTick legStart, MusicalTick newDuration,
                                                      MusicalTick now) noexcept {
    const MusicalTick deadline = legStart + newDuration;
    return deadline < now ? now : deadline;
}

} // namespace signalflux::engine
