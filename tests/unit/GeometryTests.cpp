// SignalFlux — route geometry and leg timing tests (D2, D33).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Geometry.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace signalflux;

TEST_CASE("Manhattan length is |dx| + |dy|, symmetric", "[domain][geometry]") {
    CHECK(manhattanLength({0, 0}, {3, 2}) == 5);
    CHECK(manhattanLength({3, 2}, {0, 0}) == 5);
    CHECK(manhattanLength({5, 5}, {5, 9}) == 4);
    CHECK(manhattanLength({2, 7}, {2, 7}) == 0);
    CHECK(manhattanLength({-3, 0}, {3, 0}) == 6);
}

TEST_CASE("Manhattan length does not overflow at extreme coordinates", "[domain][geometry]") {
    constexpr auto big = std::numeric_limits<std::int32_t>::max();
    constexpr auto small = std::numeric_limits<std::int32_t>::min();
    CHECK(manhattanLength({small, small}, {big, big}) == 2 * (std::int64_t{big} - small));
}

TEST_CASE("D2 worked example: (0,0)->(3,2) at default spacing is 4800 ticks", "[domain][geometry]") {
    const auto d = legDuration(manhattanLength({0, 0}, {3, 2}), GridSpacing{});
    REQUIRE(d);
    CHECK(d->value() == 4800);
}

TEST_CASE("Grid spacing scales leg duration", "[domain][geometry]") {
    CHECK(legDuration(4, *GridSpacing::make(480))->value() == 1920); // 1/8 spacing halves
    CHECK(legDuration(3, *GridSpacing::make(320))->value() == 960);  // three 1/8 triplets = one beat
}

TEST_CASE("D33 worked example: 3 cells at x3/2 takes 2 beats", "[domain][geometry]") {
    const auto rate = *RouteRate::make(3, 2);
    const auto d = legDuration(3, GridSpacing{}, rate);
    REQUIRE(d);
    CHECK(d->value() == 2 * kTicksPerQuarter);
}

TEST_CASE("Every menu rate is exact at default spacing", "[domain][geometry]") {
    for (const auto n : RouteRate::kMenu)
        for (const auto dd : RouteRate::kMenu) {
            const auto rate = *RouteRate::make(n, dd);
            CHECK(isRateExact(GridSpacing{}, rate));
            for (std::int64_t len = 1; len <= 16; ++len) CHECK(legDuration(len, GridSpacing{}, rate));
        }
}

TEST_CASE("Inexact rates are detected rather than rounded", "[domain][geometry]") {
    const auto spacing = *GridSpacing::make(80);    // 1/32 triplet
    const auto rate = *RouteRate::make(3, 1);
    CHECK_FALSE(isRateExact(spacing, rate));
    CHECK_FALSE(legDuration(1, spacing, rate));
}

TEST_CASE("Negative lengths are rejected; zero length is zero ticks", "[domain][geometry]") {
    CHECK_FALSE(legDuration(-1, GridSpacing{}));
    CHECK(legDuration(0, GridSpacing{})->value() == 0);
}
