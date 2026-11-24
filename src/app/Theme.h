// SignalFlux — visual themes (DECISIONS D40).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>

namespace signalflux::app {

struct Theme {
    bool dark = true;
    juce::Colour canvas, gridDot, panel, panelBorder, text, textMuted, selection;
    std::array<juce::Colour, 8> stations;

    [[nodiscard]] juce::Colour station(int index) const { return stations[static_cast<std::size_t>(index & 7)]; }
};

/// Default: Night transit. Station colours from the Okabe-Ito set (colour-blind friendly).
inline const Theme& nightTransit() {
    static const Theme t{true,
                         juce::Colour(0xff10131a), juce::Colour(0xff2a3140), juce::Colour(0xff161a23),
                         juce::Colour(0xff262c3a), juce::Colour(0xffe8eaf0), juce::Colour(0xff8a93a6),
                         juce::Colour(0xffe8eaf0),
                         {juce::Colour(0xffe69f00), juce::Colour(0xff56b4e9), juce::Colour(0xff009e73),
                          juce::Colour(0xfff0e442), juce::Colour(0xff0072b2), juce::Colour(0xffd55e00),
                          juce::Colour(0xffcc79a7), juce::Colour(0xffe8eaf0)}};
    return t;
}

/// Light toggle: Paper sketch.
inline const Theme& paperSketch() {
    static const Theme t{false,
                         juce::Colour(0xfff4f1ea), juce::Colour(0xffc9c3b6), juce::Colour(0xffebe7dd),
                         juce::Colour(0xffd6d0c2), juce::Colour(0xff22252b), juce::Colour(0xff6b6860),
                         juce::Colour(0xff22252b),
                         {juce::Colour(0xffd1495b), juce::Colour(0xff30638e), juce::Colour(0xff00798c),
                          juce::Colour(0xffedae49), juce::Colour(0xff8e5572), juce::Colour(0xff66a182),
                          juce::Colour(0xffe07a5f), juce::Colour(0xff3d405b)}};
    return t;
}

} // namespace signalflux::app
