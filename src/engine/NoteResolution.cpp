// SignalFlux — note resolution (task SF-004).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "engine/NoteResolution.h"

#include <utility>

namespace signalflux::engine {

namespace {

constexpr std::int64_t floorDiv(std::int64_t a, std::int64_t b) noexcept {
    const std::int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

int bitsBelow(std::uint16_t mask, std::int64_t pc) noexcept {
    int n = 0;
    for (std::int64_t i = 0; i < pc; ++i) n += (mask >> i) & 1;
    return n;
}

} // namespace

bool inScale(std::int64_t pitch, ScaleContext s) noexcept {
    return s.mask.containsInterval(static_cast<int>(((pitch - s.root.value()) % 12 + 12) % 12));
}

std::int64_t degreeIndex(std::int64_t pitch, ScaleContext s) noexcept {
    const std::int64_t rel = pitch - s.root.value();
    const std::int64_t octave = floorDiv(rel, 12);
    const std::int64_t pc = rel - octave * 12;
    return octave * s.mask.degreeCount() + bitsBelow(s.mask.bits(), pc);
}

std::int64_t pitchAtDegree(std::int64_t index, ScaleContext s) noexcept {
    const std::int64_t degrees = s.mask.degreeCount();
    const std::int64_t octave = floorDiv(index, degrees);
    std::int64_t remaining = index - octave * degrees;
    for (std::int64_t pc = 0; pc < 12; ++pc) {
        if (((s.mask.bits() >> pc) & 1) == 0) continue;
        if (remaining-- == 0) return s.root.value() + octave * 12 + pc;
    }
    return s.root.value() + octave * 12; // unreachable: mask is never empty
}

std::int64_t stepInScale(std::int64_t reference, std::int64_t steps, ScaleContext s) noexcept {
    const std::int64_t idx = degreeIndex(reference, s); // count of in-scale pitches strictly below
    if (inScale(reference, s)) return pitchAtDegree(idx + steps, s);
    if (steps > 0) return pitchAtDegree(idx + steps - 1, s); // first above has index idx
    if (steps < 0) return pitchAtDegree(idx + steps, s);     // first below has index idx - 1
    const std::int64_t below = pitchAtDegree(idx - 1, s);
    const std::int64_t above = pitchAtDegree(idx, s);
    return (reference - below) <= (above - reference) ? below : above; // ties down
}

std::int64_t forceToScale(std::int64_t pitch, ScaleContext s) noexcept {
    if (inScale(pitch, s)) return pitch;
    const std::int64_t idx = degreeIndex(pitch, s);
    const std::int64_t below = pitchAtDegree(idx - 1, s);
    const std::int64_t above = pitchAtDegree(idx, s);
    const bool belowOk = below >= 0;
    const bool aboveOk = above <= 127;
    if (belowOk && !aboveOk) return below;
    if (aboveOk && !belowOk) return above;
    return (pitch - below) <= (above - pitch) ? below : above; // ties down
}

std::int64_t foldToMidiRange(std::int64_t pitch) noexcept {
    if (pitch > 127) pitch -= ((pitch - 127 + 11) / 12) * 12;
    if (pitch < 0) pitch += ((-pitch + 11) / 12) * 12;
    return pitch;
}

namespace {

/// Walk mode (D10): reflect a degree distance back inside [-bound, +bound].
std::int64_t reflectIntoBound(std::int64_t d, std::int64_t bound) noexcept {
    if (bound == 0) return 0;
    const std::int64_t period = 4 * bound; // reflection is periodic: -B..B..-B
    std::int64_t x = ((d + bound) % period + period) % period; // 0..4B-1, 0 corresponds to -B
    if (x > 2 * bound) x = period - x;
    return x - bound;
}

MidiNote toMidi(std::int64_t pitch) noexcept { return *MidiNote::make(foldToMidiRange(pitch)); }

} // namespace

ResolvedNotes resolveNotes(const NoteSettings& n, ScaleContext scale, const NoteContext& incoming, IntDraw& draw) {
    const std::int64_t reference =
        incoming.note ? incoming.note->value() : defaultReference(scale.root);

    ResolvedNotes out;
    out.outgoing.relativeOffset = incoming.relativeOffset;
    std::int64_t root = 0;

    if (n.pass) {
        root = reference; // Pass overrides every mode, Relative included
    } else {
        switch (n.mode) {
            case NoteMode::Absolute:
                root = n.forceScale ? forceToScale(n.absoluteNote.value(), scale) : n.absoluteNote.value();
                break;
            case NoteMode::Relative: {
                const std::int32_t steps = n.relativePassThrough ? incoming.relativeOffset : n.relativeSteps;
                root = stepInScale(reference, steps, scale);
                out.outgoing.relativeOffset = steps;
                break;
            }
            case NoteMode::RandomValue: {
                const std::int64_t k = draw.draw(-n.random.rangeSteps, n.random.rangeSteps);
                const std::int64_t anchor = stepInScale(n.random.anchor.value(), 0, scale);
                if (n.random.mode == RandomValueMode::Anchor) {
                    root = stepInScale(anchor, k, scale);
                } else {
                    const std::int64_t candidate = stepInScale(reference, k, scale);
                    const std::int64_t distance = degreeIndex(candidate, scale) - degreeIndex(anchor, scale);
                    root = pitchAtDegree(degreeIndex(anchor, scale) + reflectIntoBound(distance, n.random.boundSteps), scale);
                }
                break;
            }
        }
    }

    const MidiNote rootNote = toMidi(root);
    out.outgoing.note = rootNote; // D19 / D34: context is the chord root

    if (n.chordOffsets.empty()) {
        out.pitches[0] = rootNote;
        out.count = 1;
        return out;
    }
    std::array<std::int64_t, limits::kMaxChordTones> tones{};
    std::size_t count = 0;
    for (const auto offset : n.chordOffsets) {
        if (count == tones.size()) break;
        tones[count++] = foldToMidiRange(offset == 0 ? rootNote.value() : stepInScale(rootNote.value(), offset, scale));
    }
    // Insertion sort + dedupe on at most 6 values. (std::sort here trips a GCC
    // -O3 -Warray-bounds false positive on small std::arrays.)
    for (std::size_t i = 1; i < count; ++i)
        for (std::size_t j = i; j > 0 && tones[j - 1] > tones[j]; --j) std::swap(tones[j - 1], tones[j]);
    for (std::size_t i = 0; i < count; ++i)
        if (out.count == 0 || out.pitches[out.count - 1].value() != tones[i])
            out.pitches[out.count++] = *MidiNote::make(tones[i]);
    return out;
}

} // namespace signalflux::engine
