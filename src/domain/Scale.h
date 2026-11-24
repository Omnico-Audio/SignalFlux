// SignalFlux — Document Scale representation (DECISIONS D11).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#pragma once

#include "domain/Types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace signalflux {

/// A set of pitch classes relative to the Document Root, as a 12-bit mask.
/// Bit i set means "root + i semitones is in the scale". Never empty.
class ScaleMask {
public:
    constexpr ScaleMask() noexcept = default; // Major

    [[nodiscard]] static constexpr std::optional<ScaleMask> make(std::uint32_t bits) noexcept {
        if (bits == 0 || bits > 0x0FFFu) return std::nullopt;
        return ScaleMask{static_cast<std::uint16_t>(bits)};
    }

    [[nodiscard]] constexpr std::uint16_t bits() const noexcept { return bits_; }

    /// True if the interval above the root (0–11) is in the scale.
    [[nodiscard]] constexpr bool containsInterval(int semitonesAboveRoot) const noexcept {
        const int i = ((semitonesAboveRoot % 12) + 12) % 12;
        return ((bits_ >> i) & 1u) != 0;
    }

    /// True if an absolute MIDI pitch is in the scale for the given root.
    [[nodiscard]] constexpr bool containsPitch(int midiPitch, PitchClass root) const noexcept {
        return containsInterval(midiPitch - root.value());
    }

    [[nodiscard]] constexpr int degreeCount() const noexcept {
        int n = 0;
        for (int i = 0; i < 12; ++i) n += (bits_ >> i) & 1;
        return n;
    }

    friend constexpr auto operator<=>(const ScaleMask&, const ScaleMask&) = default;

private:
    constexpr explicit ScaleMask(std::uint16_t b) noexcept : bits_(b) {}
    std::uint16_t bits_ = 0b1010'1011'0101; // Major: 0 2 4 5 7 9 11
};

enum class ScalePreset : std::uint8_t {
    Major,
    NaturalMinor,
    HarmonicMinor,
    MelodicMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian,
    MajorPentatonic,
    MinorPentatonic,
    Blues,
    Chromatic,
};

struct ScalePresetInfo {
    ScalePreset preset;
    std::string_view name;
    ScaleMask mask;
};

namespace detail {
constexpr ScaleMask maskFromIntervals(std::initializer_list<int> intervals) {
    std::uint32_t bits = 0;
    for (const int i : intervals) bits |= 1u << static_cast<unsigned>(i);
    return *ScaleMask::make(bits);
}
} // namespace detail

inline constexpr std::array<ScalePresetInfo, 13> kScalePresets{{
    {ScalePreset::Major,           "Major",            detail::maskFromIntervals({0, 2, 4, 5, 7, 9, 11})},
    {ScalePreset::NaturalMinor,    "Natural Minor",    detail::maskFromIntervals({0, 2, 3, 5, 7, 8, 10})},
    {ScalePreset::HarmonicMinor,   "Harmonic Minor",   detail::maskFromIntervals({0, 2, 3, 5, 7, 8, 11})},
    {ScalePreset::MelodicMinor,    "Melodic Minor",    detail::maskFromIntervals({0, 2, 3, 5, 7, 9, 11})},
    {ScalePreset::Dorian,          "Dorian",           detail::maskFromIntervals({0, 2, 3, 5, 7, 9, 10})},
    {ScalePreset::Phrygian,        "Phrygian",         detail::maskFromIntervals({0, 1, 3, 5, 7, 8, 10})},
    {ScalePreset::Lydian,          "Lydian",           detail::maskFromIntervals({0, 2, 4, 6, 7, 9, 11})},
    {ScalePreset::Mixolydian,      "Mixolydian",       detail::maskFromIntervals({0, 2, 4, 5, 7, 9, 10})},
    {ScalePreset::Locrian,         "Locrian",          detail::maskFromIntervals({0, 1, 3, 5, 6, 8, 10})},
    {ScalePreset::MajorPentatonic, "Major Pentatonic", detail::maskFromIntervals({0, 2, 4, 7, 9})},
    {ScalePreset::MinorPentatonic, "Minor Pentatonic", detail::maskFromIntervals({0, 3, 5, 7, 10})},
    {ScalePreset::Blues,           "Blues",            detail::maskFromIntervals({0, 3, 5, 6, 7, 10})},
    {ScalePreset::Chromatic,       "Chromatic",        detail::maskFromIntervals({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11})},
}};

[[nodiscard]] constexpr ScaleMask maskFor(ScalePreset preset) noexcept {
    for (const auto& info : kScalePresets)
        if (info.preset == preset) return info.mask;
    return ScaleMask{};
}

/// The preset whose mask equals `mask`, if any (custom scales return nullopt).
[[nodiscard]] constexpr std::optional<ScalePreset> presetFor(ScaleMask mask) noexcept {
    for (const auto& info : kScalePresets)
        if (info.mask == mask) return info.preset;
    return std::nullopt;
}

} // namespace signalflux
