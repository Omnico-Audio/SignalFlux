// SignalFlux — internal synth tests (SF-008; ACCEPTANCE §4).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "audio/Synth.h"
#include "domain/Examples.h"
#include "engine/EventLog.h"
#include "engine/Sequencer.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using signalflux::audio::Synth;

namespace {
float peak(Synth& s, int samples) {
    std::vector<float> l(static_cast<std::size_t>(samples)), r(static_cast<std::size_t>(samples));
    s.render(l.data(), r.data(), samples);
    float p = 0.0f;
    for (std::size_t i = 0; i < l.size(); ++i) p = std::max({p, std::fabs(l[i]), std::fabs(r[i])});
    return p;
}
} // namespace

TEST_CASE("16 simultaneous voices", "[audio][synth]") {
    Synth s;
    s.prepare(48'000);
    for (int i = 0; i < 16; ++i) s.noteOn(1, 40 + i, 100);
    CHECK(s.activeVoices() == 16);
    for (int i = 0; i < 16; ++i) CHECK(s.voicePitch(i) == 40 + i);
    CHECK(peak(s, 480) > 0.0f);
}

TEST_CASE("Voice stealing takes the oldest held voice when all are busy", "[audio][synth]") {
    Synth s;
    s.prepare(48'000);
    for (int i = 0; i < 16; ++i) s.noteOn(1, 40 + i, 100);
    s.noteOn(1, 100, 100);
    CHECK(s.voicePitch(0) == 100); // the first note (oldest) was replaced
    s.noteOn(1, 101, 100);
    CHECK(s.voicePitch(1) == 101);
    CHECK(s.activeVoices() == 16);
}

TEST_CASE("Releasing voices are stolen before held ones", "[audio][synth]") {
    Synth s;
    s.prepare(48'000);
    for (int i = 0; i < 16; ++i) s.noteOn(1, 40 + i, 100);
    s.noteOff(1, 50); // voice 10 starts releasing
    s.noteOn(1, 90, 100);
    CHECK(s.voicePitch(10) == 90);
    CHECK(s.voicePitch(0) == 40); // oldest held voice survived
}

TEST_CASE("Voice stealing is deterministic", "[audio][synth][determinism]") {
    auto run = [] {
        Synth s;
        s.prepare(44'100);
        std::vector<int> pitches;
        for (int i = 0; i < 40; ++i) {
            s.noteOn((i % 3) + 1, 30 + (i * 7) % 60, 64);
            if (i % 5 == 0) s.noteOff((i % 3) + 1, 30 + (i * 7) % 60);
        }
        for (int v = 0; v < Synth::kVoices; ++v) pitches.push_back(s.voicePitch(v));
        return pitches;
    };
    CHECK(run() == run());
}

TEST_CASE("Note Off releases every voice holding that channel and pitch (D22)", "[audio][synth]") {
    Synth s;
    s.prepare(48'000);
    s.noteOn(1, 60, 100);
    s.noteOn(1, 60, 100); // overlapping same pitch: two voices
    s.noteOn(2, 60, 100); // other channel
    s.noteOff(1, 60);
    CHECK_FALSE(s.voiceHeld(0));
    CHECK_FALSE(s.voiceHeld(1));
    CHECK(s.voiceHeld(2));
}

TEST_CASE("Released voices fade to silence; allNotesOff silences everything", "[audio][synth]") {
    Synth s;
    s.prepare(48'000);
    s.noteOn(1, 60, 127);
    s.noteOn(5, 67, 127);
    (void)peak(s, 4'800);
    s.allNotesOff();
    (void)peak(s, 48'000); // longer than the release time
    CHECK(s.activeVoices() == 0);
    CHECK(peak(s, 480) == 0.0f);
}

TEST_CASE("The synth consumes engine events without changing them", "[audio][synth]") {
    // Rendering the same project with and without feeding a synth gives the same log:
    // the synth sits downstream of the engine and has no way to affect it.
    const auto doc = *signalflux::examples::make("polyrhythm");
    const signalflux::MusicalTick len{16 * signalflux::kTicksPerQuarter};
    const auto before = signalflux::engine::renderCanonicalLog(doc, len);
    Synth s;
    s.prepare(48'000);
    signalflux::engine::EventLog log;
    signalflux::engine::Sequencer seq{log};
    REQUIRE(seq.setDocument(doc).ok());
    seq.play();
    seq.processUntil(len);
    seq.requestStop();
    if (seq.pendingStop()) seq.processUntil(*seq.pendingStop());
    for (const auto& e : log.events()) {
        if (e.type == signalflux::engine::EngineEventType::NoteOn) s.noteOn(e.channel.value(), e.note.value(), e.velocity.value());
        if (e.type == signalflux::engine::EngineEventType::NoteOff) s.noteOff(e.channel.value(), e.note.value());
    }
    CHECK(log.canonical() == before);
}
