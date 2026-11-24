// SignalFlux — note resolution (task SF-004; DECISIONS D9, D10, D11, D19, D34, D39).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Pure functions: no engine state, no randomness of their own (draws are passed
// in), so every rule is unit-testable in isolation.
//
// Scale arithmetic works on unbounded integers ("pitch" = MIDI number that may
// lie outside 0–127 before folding). A scale degree index numbers every
// in-scale pitch in order; stepping k scale steps is index arithmetic.

#pragma once

#include "domain/Project.h"
#include "domain/Scale.h"

#include <array>
#include <cstdint>
#include <optional>

namespace signalflux::engine {

struct ScaleContext {
    PitchClass root;
    ScaleMask mask;
};

/// Number of in-scale pitches strictly below `pitch` (offset so that the
/// Document Root in octave 0 relative numbering is index 0). For an in-scale
/// pitch this is its exact degree index.
[[nodiscard]] std::int64_t degreeIndex(std::int64_t pitch, ScaleContext s) noexcept;

/// The in-scale pitch with the given degree index.
[[nodiscard]] std::int64_t pitchAtDegree(std::int64_t index, ScaleContext s) noexcept;

[[nodiscard]] bool inScale(std::int64_t pitch, ScaleContext s) noexcept;

/// D9: +k = k-th in-scale pitch strictly above `reference`; −k = k-th strictly
/// below; 0 = reference if in scale, else the nearest in-scale pitch (ties down).
[[nodiscard]] std::int64_t stepInScale(std::int64_t reference, std::int64_t steps, ScaleContext s) noexcept;

/// D11: snap to the nearest in-scale pitch, ties downward, staying in 0–127 when possible.
[[nodiscard]] std::int64_t forceToScale(std::int64_t pitch, ScaleContext s) noexcept;

/// D10: fold by octaves into 0–127 (preserves pitch class, so scale membership).
[[nodiscard]] std::int64_t foldToMidiRange(std::int64_t pitch) noexcept;

/// D9: reference used when no note context exists: the root in octave 4.
[[nodiscard]] constexpr std::int64_t defaultReference(PitchClass root) noexcept { return 60 + root.value(); }

/// Signal context carried by a Traveler (D19, D39). Explicit runtime data,
/// never hidden state on the Station.
struct NoteContext {
    std::optional<MidiNote> note;      ///< Previous *resolved* note (D19).
    std::int32_t relativeOffset = 0;   ///< Last offset applied by a Relative Station (D39).
};

struct ResolvedNotes {
    std::array<MidiNote, limits::kMaxChordTones> pitches{}; ///< Ascending, unique.
    std::size_t count = 0;
    NoteContext outgoing;                                   ///< Context passed onward.
};

/// Draws an integer uniformly in [lo, hi]. Called at most once per resolution.
/// An interface rather than std::function so the engine path never allocates.
class IntDraw {
public:
    virtual ~IntDraw() = default;
    virtual std::int64_t draw(std::int64_t lo, std::int64_t hi) noexcept = 0;
};

/// Resolves a Note Station's pitches from its settings, the Document Scale, and
/// the incoming context. Rules: Pass > mode (BEHAVIORAL §3); Relative steps from
/// the carried note (D9); Relative Pass-through reuses the incoming offset
/// (D39); Random Value Anchor/Walk (D10); Force Scale for Absolute only (D11);
/// chords from the resolved root (D34); octave folding (D10).
[[nodiscard]] ResolvedNotes resolveNotes(const NoteSettings& settings, ScaleContext scale, const NoteContext& incoming,
                                         IntDraw& draw);

} // namespace signalflux::engine
