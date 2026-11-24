// SignalFlux — drawn route geometry (D2, D31) shared by the canvas, Traveler
// sprites and beat ticks. Coordinates are in grid cells (floating point is fine
// here: this is presentation only, never timing).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#pragma once

#include "domain/Geometry.h"
#include "domain/Project.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace signalflux::geometry {

struct PointF {
    double x = 0.0, y = 0.0;
};

/// Orthogonal route with one elbow: from -> corner -> to (D2).
struct Route {
    std::array<PointF, 3> points;
    double length = 0.0; // Manhattan length in cells
};

[[nodiscard]] inline Route routeBetween(GridCoordinate a, GridCoordinate b, RouteElbow elbow) noexcept {
    const PointF from{static_cast<double>(a.x), static_cast<double>(a.y)};
    const PointF to{static_cast<double>(b.x), static_cast<double>(b.y)};
    const PointF corner = elbow == RouteElbow::HorizontalFirst ? PointF{to.x, from.y} : PointF{from.x, to.y};
    return Route{{from, corner, to}, static_cast<double>(manhattanLength(a, b))};
}

/// Point at `fraction` (0–1, clamped) of the way along the route.
[[nodiscard]] inline PointF pointAlong(const Route& r, double fraction) noexcept {
    if (r.length <= 0.0) return r.points[0];
    double d = std::clamp(fraction, 0.0, 1.0) * r.length;
    for (std::size_t i = 0; i + 1 < r.points.size(); ++i) {
        const auto& p = r.points[i];
        const auto& q = r.points[i + 1];
        const double seg = std::abs(q.x - p.x) + std::abs(q.y - p.y);
        if (d <= seg || i + 2 == r.points.size()) {
            const double t = seg > 0.0 ? std::min(d / seg, 1.0) : 0.0;
            return PointF{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t};
        }
        d -= seg;
    }
    return r.points.back();
}

/// D31: fractions along the route (0–1, exclusive of the ends) where a beat
/// boundary falls, for a leg lasting `legTicks`. Every 4th beat is "strong".
struct BeatTick {
    double fraction = 0.0;
    bool strong = false;
};

[[nodiscard]] inline std::vector<BeatTick> beatTicks(std::int64_t legTicks) {
    std::vector<BeatTick> out;
    if (legTicks <= 0) return out;
    for (std::int64_t t = kTicksPerQuarter, n = 1; t < legTicks; t += kTicksPerQuarter, ++n)
        out.push_back(BeatTick{static_cast<double>(t) / static_cast<double>(legTicks), n % 4 == 0});
    return out;
}

} // namespace signalflux::geometry
