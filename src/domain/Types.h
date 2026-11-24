// SignalFlux — domain strong types.
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Thread ownership: plain values, freely copyable; no shared mutable state.
// Dependencies: standard library only (no JUCE), per ARCHITECTURE §2.

#pragma once

#include <compare>
#include <cstdint>
#include <numeric>
#include <optional>

namespace signalflux {

// ---------------------------------------------------------------------------
// Time base (DECISIONS D18, D25)
// ---------------------------------------------------------------------------

/// Internal resolution. Divisible by 2^6 and 3, so straight and triplet
/// note values are exact integers.
inline constexpr std::int64_t kTicksPerQuarter = 960;

/// MIDI Clock resolution, fixed by the MIDI 1.0 specification.
inline constexpr std::int64_t kMidiClocksPerQuarter = 24;
inline constexpr std::int64_t kTicksPerMidiClock = kTicksPerQuarter / kMidiClocksPerQuarter; // 40
static_assert(kTicksPerQuarter % kMidiClocksPerQuarter == 0);

/// A position or span on the canonical integer musical timeline.
class MusicalTick {
public:
    constexpr MusicalTick() noexcept = default;
    constexpr explicit MusicalTick(std::int64_t ticks) noexcept : ticks_(ticks) {}

    [[nodiscard]] constexpr std::int64_t value() const noexcept { return ticks_; }

    friend constexpr auto operator<=>(const MusicalTick&, const MusicalTick&) = default;
    friend constexpr MusicalTick operator+(MusicalTick a, MusicalTick b) noexcept { return MusicalTick{a.ticks_ + b.ticks_}; }
    friend constexpr MusicalTick operator-(MusicalTick a, MusicalTick b) noexcept { return MusicalTick{a.ticks_ - b.ticks_}; }

private:
    std::int64_t ticks_ = 0;
};

// ---------------------------------------------------------------------------
// Identifiers. Value 0 is reserved as "invalid / unassigned".
// ---------------------------------------------------------------------------

template <typename Tag>
class StrongId {
public:
    constexpr StrongId() noexcept = default;
    constexpr explicit StrongId(std::uint32_t v) noexcept : value_(v) {}

    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != 0; }

    friend constexpr auto operator<=>(const StrongId&, const StrongId&) = default;

private:
    std::uint32_t value_ = 0;
};

using PlotId      = StrongId<struct PlotIdTag>;
using StationId   = StrongId<struct StationIdTag>;
using FlowpointId = StrongId<struct FlowpointIdTag>;
using GateId      = StrongId<struct GateIdTag>;
using SwitchId    = StrongId<struct SwitchIdTag>;
using SceneId     = StrongId<struct SceneIdTag>;
using MappingId   = StrongId<struct MappingIdTag>;
using TravelerId  = StrongId<struct TravelerIdTag>;

// ---------------------------------------------------------------------------
// Bounded integer values. Construction is only possible with an in-range value:
//   make(v)  -> std::optional (runtime input, e.g. UI or file)
//   of<V>()  -> compile-time checked constant
// Default construction yields the documented default.
// ---------------------------------------------------------------------------

template <typename Tag, int Min, int Max, int Default>
class BoundedInt {
    static_assert(Min <= Default && Default <= Max);

public:
    static constexpr int kMin = Min;
    static constexpr int kMax = Max;
    static constexpr int kDefault = Default;

    constexpr BoundedInt() noexcept = default;

    [[nodiscard]] static constexpr std::optional<BoundedInt> make(std::int64_t v) noexcept {
        if (v < Min || v > Max) return std::nullopt;
        return BoundedInt{static_cast<int>(v)};
    }

    template <int V>
    [[nodiscard]] static consteval BoundedInt of() noexcept {
        static_assert(V >= Min && V <= Max, "value out of range");
        return BoundedInt{V};
    }

    [[nodiscard]] constexpr int value() const noexcept { return value_; }

    friend constexpr auto operator<=>(const BoundedInt&, const BoundedInt&) = default;

private:
    constexpr explicit BoundedInt(int v) noexcept : value_(v) {}
    int value_ = Default;
};

/// MIDI channel as shown to users, 1–16. Wire encoding (0–15) happens in the MIDI layer.
using MidiChannel        = BoundedInt<struct MidiChannelTag, 1, 16, 1>;
using MidiNote           = BoundedInt<struct MidiNoteTag, 0, 127, 60>;
/// Note On velocity, 1–127. Velocity 0 is not a valid Station velocity: on the
/// wire it means Note Off, so it is excluded from the domain (ARCHITECTURE §4).
using Velocity           = BoundedInt<struct VelocityTag, 1, 127, 100>;
using ProbabilityPercent = BoundedInt<struct ProbabilityTag, 0, 100, 100>;
/// Controllers 120–127 are channel-mode messages and are reserved for Panic.
using CcNumber           = BoundedInt<struct CcNumberTag, 0, 119, 1>;
using CcValue            = BoundedInt<struct CcValueTag, 0, 127, 0>;
using PitchBendValue     = BoundedInt<struct PitchBendTag, -8192, 8191, 0>;
using PitchClass         = BoundedInt<struct PitchClassTag, 0, 11, 0>;
/// Weighted Random branching (D32). 0 = never chosen by Random.
using RouteWeight        = BoundedInt<struct RouteWeightTag, 0, 100, 1>;
/// Tempo in thousandths of a BPM (D25: no floating point). 20–300 BPM.
using MilliBpm           = BoundedInt<struct MilliBpmTag, 20'000, 300'000, 120'000>;

// ---------------------------------------------------------------------------
// Grid geometry
// ---------------------------------------------------------------------------

struct GridCoordinate {
    std::int32_t x = 0;
    std::int32_t y = 0;
    friend constexpr auto operator<=>(const GridCoordinate&, const GridCoordinate&) = default;
};

/// Ticks per grid cell. Restricted to musical note values (D18) so every
/// leg duration is an exact integer.
class GridSpacing {
public:
    constexpr GridSpacing() noexcept = default;

    [[nodiscard]] static constexpr std::optional<GridSpacing> make(std::int64_t ticks) noexcept {
        for (const auto allowed : kAllowedTicks)
            if (ticks == allowed) return GridSpacing{ticks};
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::int64_t ticks() const noexcept { return ticks_; }

    friend constexpr auto operator<=>(const GridSpacing&, const GridSpacing&) = default;

    /// 1/1 … 1/32 straight, then the triplet value of each.
    static constexpr std::int64_t kAllowedTicks[] = {
        3840, 1920, 960, 480, 240, 120,
        2560, 1280, 640, 320, 160, 80,
    };

private:
    constexpr explicit GridSpacing(std::int64_t t) noexcept : ticks_(t) {}
    std::int64_t ticks_ = kTicksPerQuarter;
};

/// Per-route speed (D33): leg duration is multiplied by den/num.
/// Numerator and denominator come from a fixed menu; stored in lowest terms.
class RouteRate {
public:
    constexpr RouteRate() noexcept = default;

    [[nodiscard]] static constexpr std::optional<RouteRate> make(std::int32_t num, std::int32_t den) noexcept {
        if (!isMenuValue(num) || !isMenuValue(den)) return std::nullopt;
        const auto g = std::gcd(num, den);
        return RouteRate{num / g, den / g};
    }

    [[nodiscard]] constexpr std::int32_t numerator() const noexcept { return num_; }
    [[nodiscard]] constexpr std::int32_t denominator() const noexcept { return den_; }

    friend constexpr auto operator<=>(const RouteRate&, const RouteRate&) = default;

    static constexpr std::int32_t kMenu[] = {1, 2, 3, 4, 5, 6, 8};

private:
    constexpr RouteRate(std::int32_t n, std::int32_t d) noexcept : num_(n), den_(d) {}
    static constexpr bool isMenuValue(std::int32_t v) noexcept {
        for (const auto m : kMenu)
            if (v == m) return true;
        return false;
    }
    std::int32_t num_ = 1;
    std::int32_t den_ = 1;
};

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

struct ProjectSchemaVersion {
    std::uint32_t value = 1;
    static constexpr std::uint32_t kCurrent = 1;
    friend constexpr auto operator<=>(const ProjectSchemaVersion&, const ProjectSchemaVersion&) = default;
};

} // namespace signalflux
