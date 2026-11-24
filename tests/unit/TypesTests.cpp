// SignalFlux — strong type tests (SF-002: "Validate MIDI channels 1–16, notes 0–127, velocity, probability 0–100").
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Types.h"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace signalflux;

TEST_CASE("MidiChannel accepts exactly 1–16", "[domain][types]") {
    CHECK_FALSE(MidiChannel::make(0));
    CHECK(MidiChannel::make(1)->value() == 1);
    CHECK(MidiChannel::make(16)->value() == 16);
    CHECK_FALSE(MidiChannel::make(17));
    CHECK_FALSE(MidiChannel::make(-1));
    CHECK(MidiChannel{}.value() == 1);
    STATIC_CHECK(MidiChannel::of<10>().value() == 10);
}

TEST_CASE("MidiNote accepts exactly 0–127", "[domain][types]") {
    CHECK(MidiNote::make(0)->value() == 0);
    CHECK(MidiNote::make(127)->value() == 127);
    CHECK_FALSE(MidiNote::make(128));
    CHECK_FALSE(MidiNote::make(-1));
    CHECK(MidiNote{}.value() == 60);
}

TEST_CASE("Velocity excludes 0 because 0 means Note Off on the wire", "[domain][types]") {
    CHECK_FALSE(Velocity::make(0));
    CHECK(Velocity::make(1)->value() == 1);
    CHECK(Velocity::make(127)->value() == 127);
    CHECK_FALSE(Velocity::make(128));
    CHECK(Velocity{}.value() == 100);
}

TEST_CASE("ProbabilityPercent accepts exactly 0–100", "[domain][types]") {
    CHECK(ProbabilityPercent::make(0)->value() == 0);
    CHECK(ProbabilityPercent::make(100)->value() == 100);
    CHECK_FALSE(ProbabilityPercent::make(101));
    CHECK_FALSE(ProbabilityPercent::make(-1));
    CHECK(ProbabilityPercent{}.value() == 100);
}

TEST_CASE("Other bounded values", "[domain][types]") {
    CHECK_FALSE(CcNumber::make(120)); // channel-mode messages are reserved for Panic
    CHECK(CcNumber::make(119));
    CHECK(PitchBendValue::make(-8192));
    CHECK(PitchBendValue::make(8191));
    CHECK_FALSE(PitchBendValue::make(8192));
    CHECK_FALSE(PitchClass::make(12));
    CHECK(RouteWeight{}.value() == 1);
    CHECK_FALSE(RouteWeight::make(101));
    CHECK(MilliBpm{}.value() == 120'000);
    CHECK_FALSE(MilliBpm::make(19'999));
    CHECK_FALSE(MilliBpm::make(300'001));
}

TEST_CASE("make() rejects values that would overflow int", "[domain][types]") {
    CHECK_FALSE(MidiNote::make(std::int64_t{1} << 40));
    CHECK_FALSE(MidiNote::make(-(std::int64_t{1} << 40)));
}

TEST_CASE("Ids: 0 is invalid; ordering is by value", "[domain][types]") {
    CHECK_FALSE(StationId{}.isValid());
    CHECK(StationId{1}.isValid());
    CHECK(StationId{1} < StationId{2});
    CHECK(StationId{7} == StationId{7});
}

TEST_CASE("Time base constants", "[domain][types]") {
    STATIC_CHECK(kTicksPerQuarter == 960);
    STATIC_CHECK(kTicksPerMidiClock == 40);
    CHECK(MusicalTick{100} + MusicalTick{20} == MusicalTick{120});
    CHECK(MusicalTick{100} - MusicalTick{120} == MusicalTick{-20});
}

TEST_CASE("GridSpacing allows only straight and triplet note values", "[domain][types]") {
    CHECK(GridSpacing{}.ticks() == 960);
    for (const auto t : GridSpacing::kAllowedTicks) CHECK(GridSpacing::make(t));
    CHECK_FALSE(GridSpacing::make(0));
    CHECK_FALSE(GridSpacing::make(100));
    CHECK_FALSE(GridSpacing::make(7680));
    CHECK(GridSpacing::make(320)->ticks() == 320); // 1/8 triplet
}

TEST_CASE("RouteRate uses the menu and is stored in lowest terms", "[domain][types]") {
    CHECK(RouteRate{}.numerator() == 1);
    CHECK(RouteRate{}.denominator() == 1);
    const auto r = RouteRate::make(6, 4);
    REQUIRE(r);
    CHECK(r->numerator() == 3);
    CHECK(r->denominator() == 2);
    CHECK(RouteRate::make(2, 2) == RouteRate{});
    CHECK_FALSE(RouteRate::make(7, 1));
    CHECK_FALSE(RouteRate::make(0, 1));
    CHECK_FALSE(RouteRate::make(1, 9));
}
