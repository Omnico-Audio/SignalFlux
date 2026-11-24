// SignalFlux — scale mask tests (D11).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Scale.h"

#include <catch2/catch_test_macros.hpp>

using namespace signalflux;

TEST_CASE("Default scale is Major", "[domain][scale]") {
    CHECK(ScaleMask{} == maskFor(ScalePreset::Major));
    CHECK(ScaleMask{}.degreeCount() == 7);
}

TEST_CASE("ScaleMask rejects empty and oversized masks", "[domain][scale]") {
    CHECK_FALSE(ScaleMask::make(0));
    CHECK_FALSE(ScaleMask::make(0x1000));
    CHECK(ScaleMask::make(0x0FFF));
    CHECK(ScaleMask::make(1)); // root only is a valid (if austere) scale
}

TEST_CASE("Preset degree counts", "[domain][scale]") {
    CHECK(maskFor(ScalePreset::MajorPentatonic).degreeCount() == 5);
    CHECK(maskFor(ScalePreset::Blues).degreeCount() == 6);
    CHECK(maskFor(ScalePreset::Chromatic).degreeCount() == 12);
    for (const auto& p : kScalePresets) {
        CHECK(p.mask.containsInterval(0)); // every preset contains its root
        CHECK(presetFor(p.mask) == p.preset); // presets are distinct
    }
}

TEST_CASE("containsPitch respects the root", "[domain][scale]") {
    const auto major = maskFor(ScalePreset::Major);
    const auto c = PitchClass::of<0>();
    const auto d = PitchClass::of<2>();
    CHECK(major.containsPitch(60, c));      // C in C major
    CHECK_FALSE(major.containsPitch(61, c)); // C# not in C major
    CHECK(major.containsPitch(61, d));      // C# in D major
    CHECK_FALSE(major.containsPitch(60, d)); // C not in D major
    CHECK(major.containsPitch(0, c));       // works at MIDI 0
    CHECK(major.containsPitch(127, c));     // G9 in C major
}

TEST_CASE("containsInterval wraps negative intervals", "[domain][scale]") {
    const auto major = maskFor(ScalePreset::Major);
    CHECK(major.containsInterval(-1));   // B below C
    CHECK_FALSE(major.containsInterval(-2)); // A# below C
    CHECK(major.containsInterval(12));
}

TEST_CASE("presetFor returns nullopt for custom masks", "[domain][scale]") {
    CHECK_FALSE(presetFor(*ScaleMask::make(0b1001)));
}
