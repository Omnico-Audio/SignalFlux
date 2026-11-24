// SignalFlux — note resolution and random stream tests (SF-004; D9, D10, D11, D19, D34, D39).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Examples.h"
#include "engine/NoteResolution.h"
#include "engine/Random.h"
#include "engine/Sequencer.h"
#include "TestProjects.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace signalflux;
using namespace signalflux::engine;

namespace {

const ScaleContext cMajor{PitchClass::of<0>(), maskFor(ScalePreset::Major)};

class FixedDraw final : public IntDraw {
public:
    explicit FixedDraw(std::int64_t v) : v_(v) {}
    std::int64_t draw(std::int64_t lo, std::int64_t hi) noexcept override {
        ++calls;
        lastLo = lo;
        lastHi = hi;
        return v_;
    }
    int calls = 0;
    std::int64_t lastLo = 0, lastHi = 0;

private:
    std::int64_t v_;
};

NoteContext ctx(int note, int offset = 0) { return NoteContext{*MidiNote::make(note), offset}; }

int single(const ResolvedNotes& r) {
    REQUIRE(r.count == 1);
    return r.pitches[0].value();
}

} // namespace

// --- Scale arithmetic (D9) ----------------------------------------------------------

TEST_CASE("D9 worked examples in C major", "[engine][notes]") {
    CHECK(stepInScale(60, 1, cMajor) == 62);
    CHECK(stepInScale(60, -1, cMajor) == 59);
    CHECK(stepInScale(60, 7, cMajor) == 72);
    CHECK(stepInScale(60, -7, cMajor) == 48);
    CHECK(stepInScale(60, 0, cMajor) == 60);
    // Out-of-scale reference
    CHECK(stepInScale(61, 1, cMajor) == 62);
    CHECK(stepInScale(61, -1, cMajor) == 60);
    CHECK(stepInScale(61, 0, cMajor) == 60); // tie resolves down
    CHECK(stepInScale(63, 0, cMajor) == 62); // D# -> D (tie down)
}

TEST_CASE("Positive and negative intervals round-trip across scales and roots", "[engine][notes]") {
    for (const auto& preset : kScalePresets)
        for (int root = 0; root < 12; ++root) {
            const ScaleContext s{*PitchClass::make(root), preset.mask};
            for (std::int64_t ref = 20; ref < 100; ++ref) {
                if (!inScale(ref, s)) continue;
                for (std::int64_t k = -15; k <= 15; ++k) {
                    const auto up = stepInScale(ref, k, s);
                    CHECK(inScale(up, s));
                    CHECK(stepInScale(up, -k, s) == ref);
                    if (k > 0) CHECK(up > ref);
                    if (k < 0) CHECK(up < ref);
                }
            }
        }
}

TEST_CASE("A full octave of steps moves exactly 12 semitones", "[engine][notes]") {
    for (const auto& preset : kScalePresets) {
        const ScaleContext s{PitchClass::of<0>(), preset.mask};
        CHECK(stepInScale(60, preset.mask.degreeCount(), s) == 72);
        CHECK(stepInScale(60, -preset.mask.degreeCount(), s) == 48);
    }
}

TEST_CASE("degreeIndex and pitchAtDegree are inverse for in-scale pitches, including negatives", "[engine][notes]") {
    const ScaleContext s{PitchClass::of<9>(), maskFor(ScalePreset::MinorPentatonic)};
    for (std::int64_t p = -40; p < 200; ++p)
        if (inScale(p, s)) CHECK(pitchAtDegree(degreeIndex(p, s), s) == p);
}

// --- Force Scale (D11) --------------------------------------------------------------

TEST_CASE("Force Scale: in scale, in range, idempotent for every pitch and preset", "[engine][notes]") {
    for (const auto& preset : kScalePresets)
        for (int root = 0; root < 12; ++root) {
            const ScaleContext s{*PitchClass::make(root), preset.mask};
            for (int p = 0; p <= 127; ++p) {
                const auto q = forceToScale(p, s);
                CHECK(inScale(q, s));
                CHECK(q >= 0);
                CHECK(q <= 127);
                CHECK(forceToScale(q, s) == q);
                if (inScale(p, s)) CHECK(q == p);
            }
        }
}

TEST_CASE("Force Scale ties resolve downward; boundaries stay in range", "[engine][notes]") {
    CHECK(forceToScale(61, cMajor) == 60); // C# -> C
    CHECK(forceToScale(66, cMajor) == 65); // F# -> F
    const ScaleContext rootOnly{PitchClass::of<0>(), *ScaleMask::make(1)};
    CHECK(forceToScale(127, rootOnly) == 120); // above would be 132
    CHECK(forceToScale(1, rootOnly) == 0);
}

// --- Range folding (D10) --------------------------------------------------------------

TEST_CASE("Octave folding keeps pitch class and lands in 0-127", "[engine][notes]") {
    CHECK(foldToMidiRange(128) == 116);
    CHECK(foldToMidiRange(139) == 127);
    CHECK(foldToMidiRange(140) == 116);
    CHECK(foldToMidiRange(-1) == 11);
    CHECK(foldToMidiRange(-12) == 0);
    CHECK(foldToMidiRange(300) == 120);
    for (std::int64_t p = -300; p < 400; ++p) {
        const auto f = foldToMidiRange(p);
        CHECK(f >= 0);
        CHECK(f <= 127);
        CHECK(((f - p) % 12) == 0);
    }
}

// --- resolveNotes ---------------------------------------------------------------------------

TEST_CASE("Absolute mode, with and without Force Scale", "[engine][notes]") {
    NoteSettings n;
    n.absoluteNote = MidiNote::of<61>();
    FixedDraw d{0};
    CHECK(single(resolveNotes(n, cMajor, {}, d)) == 61);
    n.forceScale = true;
    CHECK(single(resolveNotes(n, cMajor, {}, d)) == 60);
    CHECK(d.calls == 0);
}

TEST_CASE("Relative mode counts scale steps from the carried note; default reference is root octave 4",
          "[engine][notes]") {
    NoteSettings n;
    n.mode = NoteMode::Relative;
    n.relativeSteps = 2;
    FixedDraw d{0};
    CHECK(single(resolveNotes(n, cMajor, ctx(67), d)) == 71);
    CHECK(single(resolveNotes(n, cMajor, {}, d)) == 64); // 60 + two steps

    const ScaleContext aMinor{PitchClass::of<9>(), maskFor(ScalePreset::NaturalMinor)};
    n.relativeSteps = 0;
    CHECK(single(resolveNotes(n, aMinor, {}, d)) == 69); // root A in octave 4
}

TEST_CASE("Relative mode sets the outgoing offset; Pass-through forwards the incoming one (D39)", "[engine][notes]") {
    NoteSettings n;
    n.mode = NoteMode::Relative;
    n.relativeSteps = 3;
    FixedDraw d{0};
    const auto own = resolveNotes(n, cMajor, ctx(60, -2), d);
    CHECK(single(own) == 65);
    CHECK(own.outgoing.relativeOffset == 3);

    n.relativePassThrough = true;
    const auto through = resolveNotes(n, cMajor, ctx(60, -2), d);
    CHECK(single(through) == 57);                 // applied the incoming -2, not its own +3
    CHECK(through.outgoing.relativeOffset == -2); // forwarded unchanged
}

TEST_CASE("Pass borrows the carried note and overrides Relative Values", "[engine][notes]") {
    NoteSettings n;
    n.mode = NoteMode::Relative;
    n.relativeSteps = 5;
    n.pass = true;
    FixedDraw d{0};
    CHECK(single(resolveNotes(n, cMajor, ctx(67), d)) == 67);
    CHECK(single(resolveNotes(n, cMajor, {}, d)) == 60);
    n.mode = NoteMode::Absolute;
    n.absoluteNote = MidiNote::of<40>();
    CHECK(single(resolveNotes(n, cMajor, ctx(67), d)) == 67);
}

TEST_CASE("Non-Relative Stations leave the carried offset unchanged (D39)", "[engine][notes]") {
    NoteSettings n;
    FixedDraw d{0};
    CHECK(resolveNotes(n, cMajor, ctx(60, 4), d).outgoing.relativeOffset == 4);
}

TEST_CASE("Random Value Anchor draws k in [-R, R] around the anchor", "[engine][notes]") {
    NoteSettings n;
    n.mode = NoteMode::RandomValue;
    n.random.anchor = MidiNote::of<60>();
    n.random.rangeSteps = 3;
    FixedDraw down{-3};
    CHECK(single(resolveNotes(n, cMajor, ctx(90), down)) == 55); // G below C: three steps down, context ignored
    CHECK(down.lastLo == -3);
    CHECK(down.lastHi == 3);
    FixedDraw up{2};
    CHECK(single(resolveNotes(n, cMajor, {}, up)) == 64);
}

TEST_CASE("Random Value Walk steps from the carried note and reflects at +/-W", "[engine][notes]") {
    NoteSettings n;
    n.mode = NoteMode::RandomValue;
    n.random.mode = RandomValueMode::Walk;
    n.random.anchor = MidiNote::of<60>();
    n.random.rangeSteps = 2;
    n.random.boundSteps = 3;
    FixedDraw up2{2};
    CHECK(single(resolveNotes(n, cMajor, ctx(62), up2)) == 65);     // D +2 -> F (within bound)
    CHECK(single(resolveNotes(n, cMajor, ctx(65), up2)) == 62);     // F +2 = A (+5), 2 past the bound of 3 -> +1 = D
    FixedDraw down2{-2};
    CHECK(single(resolveNotes(n, cMajor, ctx(57), down2)) == 57);   // A3 (-2) -2 = F3 (-4), 1 past -3 -> -2 = A3
}

TEST_CASE("Walk never leaves the bound, for any draw", "[engine][notes]") {
    NoteSettings n;
    n.mode = NoteMode::RandomValue;
    n.random.mode = RandomValueMode::Walk;
    n.random.anchor = MidiNote::of<60>();
    n.random.rangeSteps = 24;
    n.random.boundSteps = 4;
    for (std::int64_t k = -24; k <= 24; ++k)
        for (int ref = 40; ref <= 80; ++ref) {
            FixedDraw d{k};
            const auto p = single(resolveNotes(n, cMajor, ctx(ref), d));
            const auto dist = degreeIndex(p, cMajor) - degreeIndex(60, cMajor);
            CHECK(dist >= -4);
            CHECK(dist <= 4);
        }
}

TEST_CASE("Chords stack scale steps on the resolved root; context carries the root (D34)", "[engine][notes]") {
    NoteSettings n;
    n.absoluteNote = MidiNote::of<62>();
    n.chordOffsets = {4, 0, 2}; // any order
    FixedDraw d{0};
    const auto r = resolveNotes(n, cMajor, {}, d);
    REQUIRE(r.count == 3);
    CHECK(r.pitches[0].value() == 62); // D minor triad in C major
    CHECK(r.pitches[1].value() == 65);
    CHECK(r.pitches[2].value() == 69);
    CHECK(r.outgoing.note->value() == 62);
}

TEST_CASE("Chord tones fold individually and duplicates collapse", "[engine][notes]") {
    NoteSettings n;
    n.absoluteNote = MidiNote::of<124>();
    n.chordOffsets = {0, 7}; // octave above 124 -> 136 -> folds to 124
    FixedDraw d{0};
    const auto r = resolveNotes(n, cMajor, {}, d);
    REQUIRE(r.count == 1);
    CHECK(r.pitches[0].value() == 124);
}

// --- Random streams (D10, D25) ---------------------------------------------------------

TEST_CASE("Random stream golden values are identical on every platform", "[engine][random][determinism]") {
    // If these change, every seeded patch changes: treat as a breaking change.
    CHECK(splitMix64(0) == 0xE220A8397B1DCDAFull);
    CHECK(streamValue(1, StationId{1}, RandomPurpose::Probability, 0) == streamValue(1, StationId{1}, RandomPurpose::Probability, 0));
    RandomStreams r;
    r.reset(12345, 2);
    std::uint64_t acc = 0;
    for (int i = 0; i < 1000; ++i) acc = acc * 31 + r.below(0, StationId{7}, RandomPurpose::Value, 100);
    CHECK(acc == 0x8FD5CA75C5487255ull);
}

TEST_CASE("Streams are independent per Station and purpose", "[engine][random]") {
    RandomStreams a, b;
    a.reset(99, 2);
    b.reset(99, 2);
    // Drawing from Station 2 in `a` must not change Station 1's sequence.
    for (int i = 0; i < 10; ++i) (void)a.next(1, StationId{2}, RandomPurpose::Branch);
    for (int i = 0; i < 10; ++i)
        CHECK(a.next(0, StationId{1}, RandomPurpose::Value) == b.next(0, StationId{1}, RandomPurpose::Value));
    CHECK(streamValue(5, StationId{1}, RandomPurpose::Value, 0) != streamValue(5, StationId{1}, RandomPurpose::Branch, 0));
    CHECK(streamValue(5, StationId{1}, RandomPurpose::Value, 0) != streamValue(5, StationId{2}, RandomPurpose::Value, 0));
}

TEST_CASE("between() covers its whole range and nothing else", "[engine][random]") {
    RandomStreams r;
    r.reset(3, 1);
    std::set<std::int64_t> seen;
    for (int i = 0; i < 2000; ++i) {
        const auto v = r.between(0, StationId{1}, RandomPurpose::Value, -3, 3);
        CHECK(v >= -3);
        CHECK(v <= 3);
        seen.insert(v);
    }
    CHECK(seen.size() == 7);
}

// --- In the sequencer ---------------------------------------------------------------------------

TEST_CASE("A Relative chain climbs the scale from the Starter's note", "[engine][notes][sequencer]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}).route(1, 1, 2).route(2, 2, 1);
    b.get(2).note.mode = NoteMode::Relative;
    b.get(2).note.relativeSteps = 2;
    b.get(1).note.mode = NoteMode::Relative;
    b.get(1).note.relativeSteps = 1;
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{4 * kTicksPerQuarter + 1});
    // Starter: 60 +1 -> 62; then +2 -> 65, +1 -> 67, +2 -> 71, +1 -> 72
    std::vector<int> notes;
    for (const auto& e : log.events())
        if (e.type == EngineEventType::NoteOn) notes.push_back(e.note.value());
    CHECK(notes == std::vector<int>{62, 65, 67, 71, 72});
}

TEST_CASE("A muted Station still updates the carried note (D19)", "[engine][notes][sequencer]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}).station(3, {2, 0});
    b.route(1, 1, 2).route(2, 2, 3);
    b.get(2).note.mode = NoteMode::Relative;
    b.get(2).note.relativeSteps = 4;
    b.get(2).manuallyMuted = true;
    b.get(3).note.mode = NoteMode::Relative;
    b.get(3).note.relativeSteps = 1;
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{3 * kTicksPerQuarter});
    std::vector<int> notes;
    for (const auto& e : log.events())
        if (e.type == EngineEventType::NoteOn) notes.push_back(e.note.value());
    CHECK(notes == std::vector<int>{60, 69}); // 60, (silent 67), 67 + 1 step = 69
}

TEST_CASE("Instant children inherit the parent's context", "[engine][notes][sequencer]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 64, true).station(2, {1, 0}).station(3, {0, 1});
    b.route(1, 1, 2).route(2, 1, 3);
    b.get(1).branchMode = BranchMode::Instant;
    for (const std::uint32_t id : {2u, 3u}) {
        b.get(id).note.mode = NoteMode::Relative;
        b.get(id).note.relativeSteps = -1;
    }
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{kTicksPerQuarter + 1});
    std::vector<int> notes;
    for (const auto& e : log.events())
        if (e.type == EngineEventType::NoteOn) notes.push_back(e.note.value());
    CHECK(notes == std::vector<int>{64, 62, 62});
}

TEST_CASE("Same seed, same melody; different seed, different melody", "[engine][notes][random]") {
    const auto doc = *examples::make("wanderer");
    auto run = [&](std::uint64_t seed) {
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(doc).ok());
        seq.play(seed);
        seq.processUntil(MusicalTick{64 * kTicksPerQuarter});
        CHECK(seq.currentSeed() == seed);
        return log.canonical();
    };
    CHECK(run(7) == run(7));
    CHECK(run(7) != run(8));
}
