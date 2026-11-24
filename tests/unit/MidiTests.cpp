// SignalFlux — MIDI services tests (SF-007; D14, D15, D26, D30; ACCEPTANCE §3).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "engine/Sequencer.h"
#include "midi/MidiBytes.h"
#include "midi/MidiInputRouter.h"
#include "midi/PlaybackClock.h"
#include "midi/SpscQueue.h"
#include "TestProjects.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>

using namespace signalflux;
using namespace signalflux::midi;
using engine::EngineEvent;
using engine::EngineEventType;
using engine::EventLog;
using engine::Sequencer;
using test::contains;

namespace {
constexpr std::int64_t Q = kTicksPerQuarter;
MidiMessage m3(std::uint8_t a, std::uint8_t b, std::uint8_t c) { return MidiMessage{{a, b, c}, 3}; }
MidiMessage m1(std::uint8_t a) { return MidiMessage{{a, 0, 0}, 1}; }
} // namespace

// --- Wire encoding --------------------------------------------------------------------

TEST_CASE("Note On and Note Off on all 16 channels", "[midi][bytes]") {
    for (int c = 1; c <= 16; ++c) {
        const auto ch = *MidiChannel::make(c);
        const auto n = static_cast<std::uint8_t>(c - 1);
        CHECK(noteOn(ch, MidiNote::of<60>(), Velocity::of<100>()) == m3(static_cast<std::uint8_t>(0x90 | n), 60, 100));
        CHECK(noteOff(ch, MidiNote::of<127>()) == m3(static_cast<std::uint8_t>(0x80 | n), 127, 0));
    }
}

TEST_CASE("Control Change and Pitch Bend encoding", "[midi][bytes]") {
    CHECK(controlChange(MidiChannel::of<3>(), 74, 127) == m3(0xB2, 74, 127));
    CHECK(pitchBend(MidiChannel::of<1>(), PitchBendValue{}) == m3(0xE0, 0x00, 0x40));               // centre 8192
    CHECK(pitchBend(MidiChannel::of<1>(), PitchBendValue::of<-8192>()) == m3(0xE0, 0x00, 0x00));
    CHECK(pitchBend(MidiChannel::of<16>(), PitchBendValue::of<8191>()) == m3(0xEF, 0x7F, 0x7F));
}

TEST_CASE("Transport and Song Position Pointer (D14)", "[midi][bytes]") {
    CHECK(clockPulse() == m1(0xF8));
    CHECK(start() == m1(0xFA));
    CHECK(continuePlayback() == m1(0xFB));
    CHECK(stop() == m1(0xFC));
    CHECK(songPosition(0) == m3(0xF2, 0, 0));
    CHECK(songPosition(16383) == m3(0xF2, 0x7F, 0x7F));

    std::array<MidiMessage, kMaxMessagesPerEvent> out{};
    EngineEvent e;
    e.type = EngineEventType::TransportStart;
    REQUIRE(encode(e, true, out) == 2);
    CHECK(out[0] == songPosition(0));
    CHECK(out[1] == start());
    REQUIRE(encode(e, false, out) == 1);
    CHECK(out[0] == start());
}

TEST_CASE("Panic silences all 16 channels", "[midi][bytes]") {
    std::array<MidiMessage, kMaxMessagesPerEvent> out{};
    EngineEvent e;
    e.type = EngineEventType::Panic;
    REQUIRE(encode(e, true, out) == 48);
    for (std::uint8_t c = 0; c < 16; ++c) {
        CHECK(out[c * 3u + 0] == m3(static_cast<std::uint8_t>(0xB0 | c), kAllNotesOff, 0));
        CHECK(out[c * 3u + 1] == m3(static_cast<std::uint8_t>(0xB0 | c), kAllSoundOff, 0));
        CHECK(out[c * 3u + 2] == m3(static_cast<std::uint8_t>(0xE0 | c), 0x00, 0x40));
    }
}

TEST_CASE("Traveler events produce no MIDI", "[midi][bytes]") {
    std::array<MidiMessage, kMaxMessagesPerEvent> out{};
    for (const auto t : {EngineEventType::TravelerArrive, EngineEventType::TravelerDepart, EngineEventType::TravelerEnd,
                         EngineEventType::NoteDropped}) {
        EngineEvent e;
        e.type = t;
        CHECK(encode(e, true, out) == 0);
    }
}

// --- Playback clock (integer, no drift) ----------------------------------------------------

TEST_CASE("mulDiv is exact beyond 64-bit intermediate products", "[midi][clock]") {
    CHECK(mulDiv(10, 20, 3) == 66);
    CHECK(mulDiv(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull) == 0xFFFFFFFFFFFFFFFFull);
    CHECK(mulDiv(1ull << 40, 1ull << 40, 1ull << 30) == 1ull << 50);
    CHECK(mulDiv(36'000'000'000ull, 288'000'000ull, 60'000'000'000ull) == 172'800'000ull); // product overflows 64 bits
}

TEST_CASE("At 120 BPM one quarter is exactly 500 ms", "[midi][clock]") {
    PlaybackClock c;
    c.start(MusicalTick{0}, 1'000, MilliBpm{});
    CHECK(c.tickAt(1'000 + 500'000) == MusicalTick{960});
    CHECK(c.tickAt(1'000 + 499'999) == MusicalTick{959});
    CHECK(c.timeOf(MusicalTick{960}) == 1'000 + 500'000);
    CHECK(c.timeOf(MusicalTick{1}) == 1'000 + 521); // 520.83 us, rounded up
}

TEST_CASE("No drift over 10 hours at an awkward tempo", "[midi][clock]") {
    PlaybackClock c;
    const auto tempo = *MilliBpm::make(133'333);
    c.start(MusicalTick{0}, 0, tempo);
    const std::int64_t tenHours = 10ll * 3600 * 1'000'000;
    // Exact: ticks = us * 133333 * 960 / 60e9
    const auto expected = static_cast<std::int64_t>(mulDiv(tenHours, 133'333ull * 960, 60'000'000'000ull));
    CHECK(c.tickAt(tenHours).value() == expected);
    for (std::int64_t t : {std::int64_t{0}, std::int64_t{12345}, expected / 2, expected}) {
        const auto us = c.timeOf(MusicalTick{t});
        CHECK(c.tickAt(us) == MusicalTick{t});
        if (us > 0) CHECK(c.tickAt(us - 1) < MusicalTick{t});
    }
}

TEST_CASE("Tempo changes re-anchor without skipping or repeating ticks", "[midi][clock]") {
    PlaybackClock c;
    c.start(MusicalTick{0}, 0, MilliBpm{});
    const auto before = c.tickAt(1'234'567);
    c.setTempo(*MilliBpm::make(60'000), 1'234'567);
    CHECK(c.tickAt(1'234'567) == before);
    CHECK(c.tickAt(1'234'567 + 1'000'000) > before); // keeps moving at the new rate
    // 60 BPM: one quarter per second
    CHECK((c.tickAt(c.timeOf(before) + 1'000'000) - before) == MusicalTick{960});
}

TEST_CASE("MIDI Clock: 24 pulses per quarter at every tempo", "[midi][clock]") {
    CHECK(nextClockPulse(MusicalTick{0}) == MusicalTick{0});
    CHECK(nextClockPulse(MusicalTick{1}) == MusicalTick{40});
    CHECK(nextClockPulse(MusicalTick{40}) == MusicalTick{40});
    for (const auto bpm : {20'000, 87'500, 120'000, 300'000}) {
        PlaybackClock c;
        c.start(MusicalTick{0}, 0, *MilliBpm::make(bpm));
        const auto oneQuarterUs = c.timeOf(MusicalTick{Q});
        int pulses = 0;
        for (auto p = nextClockPulse(MusicalTick{0}); p < MusicalTick{Q}; p = p + MusicalTick{kTicksPerMidiClock}) ++pulses;
        CHECK(pulses == 24);
        CHECK(c.tickAt(oneQuarterUs) == MusicalTick{Q});
    }
}

// --- Lock-free queue --------------------------------------------------------------------------

TEST_CASE("SpscQueue is bounded and FIFO", "[midi][queue]") {
    SpscQueue<int, 4> q;
    CHECK(q.push(1));
    CHECK(q.push(2));
    CHECK(q.push(3));
    CHECK(q.push(4));
    CHECK_FALSE(q.push(5)); // full: caller applies its overflow policy
    CHECK(*q.pop() == 1);
    CHECK(q.push(5));
    CHECK(*q.pop() == 2);
    CHECK(*q.pop() == 3);
    CHECK(*q.pop() == 4);
    CHECK(*q.pop() == 5);
    CHECK_FALSE(q.pop());
}

TEST_CASE("SpscQueue delivers every item across threads in order", "[midi][queue]") {
    SpscQueue<std::uint32_t, 256> q;
    constexpr std::uint32_t n = 200'000;
    std::thread producer([&] {
        for (std::uint32_t i = 0; i < n;)
            if (q.push(i)) ++i;
    });
    std::uint32_t expect = 0;
    bool ordered = true;
    while (expect < n)
        if (const auto v = q.pop()) ordered = ordered && (*v == expect++);
    producer.join();
    CHECK(ordered);
}

// --- CC and Pitch Bend Stations (D26) ------------------------------------------------------------

TEST_CASE("A CC Station sends its value; a ramp glides over the Station's duration", "[midi][engine][cc]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}).route(1, 1, 2);
    auto& s1 = b.get(1);
    s1.eventType = StationEventType::ControlChange;
    s1.controlChange = {CcNumber::of<74>(), CcValue::of<40>(), false};
    auto& s2 = b.get(2);
    s2.eventType = StationEventType::ControlChange;
    s2.controlChange = {CcNumber::of<74>(), CcValue::of<120>(), true};
    s2.note.duration = MusicalTick{100};
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{2 * Q});
    CHECK(contains(log, "0 CC ch=1 cc=74 val=40"));
    CHECK(contains(log, "970 CC ch=1 cc=74 val=48")); // 40 + 80 * 10/100
    CHECK(contains(log, "1060 CC ch=1 cc=74 val=120"));
    std::size_t steps = 0;
    for (const auto& e : log.events())
        if (e.type == EngineEventType::ControlChange && e.tick >= MusicalTick{960}) ++steps;
    CHECK(steps == 10);
    CHECK(test::count(log, EngineEventType::NoteOn) == 0); // CC Stations play no notes
}

TEST_CASE("A newer CC for the same controller replaces a running ramp", "[midi][engine][cc]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 60, false).route(1, 1, 2);
    auto& s1 = b.get(1);
    s1.eventType = StationEventType::ControlChange;
    s1.controlChange = {CcNumber::of<1>(), CcValue::of<127>(), true};
    s1.note.duration = MusicalTick{1920}; // still ramping when Station 2 fires at 960
    auto& s2 = b.get(2);
    s2.eventType = StationEventType::ControlChange;
    s2.controlChange = {CcNumber::of<1>(), CcValue::of<0>(), false};
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{3 * Q});
    CHECK(contains(log, "960 CC ch=1 cc=1 val=0"));
    for (const auto& e : log.events())
        if (e.type == EngineEventType::ControlChange && e.tick > MusicalTick{960}) FAIL("ramp kept running");
}

TEST_CASE("Pitch Bend Stations, and Stop freezes ramps", "[midi][engine][cc]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true);
    auto& s1 = b.get(1);
    s1.eventType = StationEventType::PitchBend;
    s1.pitchBend = {PitchBendValue::of<8191>(), true};
    s1.note.duration = MusicalTick{400};
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{100});
    seq.requestStop(); // at 120
    seq.processUntil(MusicalTick{120});
    seq.continuePlayback();
    seq.processUntil(MusicalTick{Q});
    CHECK(contains(log, "10 PB ch=1 val=204"));
    for (const auto& e : log.events())
        if (e.type == EngineEventType::PitchBend && e.tick > MusicalTick{120}) FAIL("ramp continued after Stop");
}

// --- MIDI input modulation (D15) ------------------------------------------------------------------

TEST_CASE("Global transpose shifts emitted notes in scale steps; context is unchanged", "[midi][engine][modulation]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}).route(1, 1, 2);
    b.get(2).note.mode = NoteMode::Relative;
    b.get(2).note.relativeSteps = 1;
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.setTranspose(2);
    seq.play();
    seq.processUntil(MusicalTick{Q + 1});
    CHECK(contains(log, "0 NoteOn ch=1 n=64 v=100 st=1 t=1"));   // C +2 steps = E
    CHECK(contains(log, "960 NoteOn ch=1 n=65 v=100 st=2 t=1")); // D (60 +1 step) +2 steps = F
}

TEST_CASE("Velocity and probability overrides apply while running", "[midi][engine][modulation]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62).route(1, 1, 2).route(2, 2, 1);
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.setVelocityOverride(StationId{2}, Velocity::of<30>());
    seq.processUntil(MusicalTick{Q + 1});
    CHECK(contains(log, "960 NoteOn ch=1 n=62 v=30 st=2 t=1"));
    seq.setProbabilityOverride(StationId{2}, ProbabilityPercent::of<0>());
    seq.processUntil(MusicalTick{3 * Q + 1});
    CHECK_FALSE(contains(log, "2880 NoteOn ch=1 n=62 v=30 st=2 t=1"));
    seq.setProbabilityOverride(StationId{2}, std::nullopt);
    seq.setVelocityOverride(StationId{2}, std::nullopt);
    seq.processUntil(MusicalTick{5 * Q + 1});
    CHECK(contains(log, "4800 NoteOn ch=1 n=62 v=100 st=2 t=1"));
}

// --- Input routing and MIDI Learn (D15, D30) ----------------------------------------------------------

namespace {
struct Recorder final : IMappingTarget {
    std::vector<std::string> calls;
    void gateNote(MidiChannel c, MidiNote n, bool held) override {
        calls.push_back("gateNote " + std::to_string(c.value()) + " " + std::to_string(n.value()) + (held ? " on" : " off"));
    }
    void gateControl(MidiChannel c, CcNumber cc, CcValue v) override {
        calls.push_back("gateCc " + std::to_string(c.value()) + " " + std::to_string(cc.value()) + " " + std::to_string(v.value()));
    }
    void transpose(std::int32_t s) override { calls.push_back("transpose " + std::to_string(s)); }
    void tempo(MilliBpm t) override { calls.push_back("tempo " + std::to_string(t.value())); }
    void stationVelocity(StationId s, Velocity v) override {
        calls.push_back("velocity " + std::to_string(s.value()) + " " + std::to_string(v.value()));
    }
    void stationProbability(StationId s, ProbabilityPercent p) override {
        calls.push_back("probability " + std::to_string(s.value()) + " " + std::to_string(p.value()));
    }
    void sceneRecall(SceneId s) override { calls.push_back("scene " + std::to_string(s.value())); }
};

void send(MidiInputRouter& r, Recorder& rec, std::initializer_list<std::uint8_t> bytes, const std::string& dev = "Pad") {
    const std::vector<std::uint8_t> v(bytes);
    (void)r.handle(v.data(), v.size(), dev, rec);
}
} // namespace

TEST_CASE("Value conversions", "[midi][router]") {
    CHECK(transposeFromCc(0, 12) == -12);
    CHECK(transposeFromCc(64, 12) == 0);
    CHECK(transposeFromCc(127, 12) == 12);
    CHECK(tempoFromCc(0).value() == 20'000);
    CHECK(tempoFromCc(127).value() == 300'000);
    CHECK(transposeFromPitchBend(-8192, 2) == -2);
    CHECK(transposeFromPitchBend(8191, 2) == 2);
    CHECK(transposeFromPitchBend(0, 2) == 0);
}

TEST_CASE("Mapped controls drive their targets; every note and CC also feeds gates", "[midi][router]") {
    MidiInputRouter r;
    MidiMapping tempo;
    tempo.id = MappingId{1};
    tempo.source = {std::nullopt, MidiChannel::of<1>(), MidiSourceType::ControlChange, 20};
    tempo.target = MappingTarget::Tempo;
    MidiMapping prob;
    prob.id = MappingId{2};
    prob.source = {std::string("Pad"), std::nullopt, MidiSourceType::ControlChange, 21};
    prob.target = MappingTarget::StationProbability;
    prob.station = StationId{7};
    r.setMappings({tempo, prob});

    Recorder rec;
    send(r, rec, {0xB0, 20, 127});
    send(r, rec, {0xB1, 20, 127});              // wrong channel for the tempo mapping
    send(r, rec, {0xB5, 21, 127}, "Other");     // wrong device for the probability mapping
    send(r, rec, {0xB5, 21, 127});
    send(r, rec, {0x92, 36, 90});
    send(r, rec, {0x92, 36, 0});                 // Note On velocity 0 = release
    CHECK(rec.calls == std::vector<std::string>{
                           "gateCc 1 20 127", "tempo 300000", "gateCc 2 20 127", "gateCc 6 21 127", "gateCc 6 21 127",
                           "probability 7 100", "gateNote 3 36 on", "gateNote 3 36 off"});
}

TEST_CASE("Scene recall fires on the rising edge only", "[midi][router]") {
    MidiInputRouter r;
    MidiMapping m;
    m.id = MappingId{1};
    m.source = {std::nullopt, std::nullopt, MidiSourceType::ControlChange, 64};
    m.target = MappingTarget::SceneRecall;
    m.scene = SceneId{3};
    r.setMappings({m});
    Recorder rec;
    for (const int v : {0, 100, 127, 10, 90}) send(r, rec, {0xB0, 64, static_cast<std::uint8_t>(v)});
    CHECK(std::count(rec.calls.begin(), rec.calls.end(), "scene 3") == 2);
}

TEST_CASE("Unsupported messages are ignored", "[midi][router]") {
    MidiInputRouter r;
    Recorder rec;
    send(r, rec, {0xC0, 5});       // Program Change
    send(r, rec, {0xF8});          // Clock
    send(r, rec, {0xF0, 0x7E, 0x7F}); // SysEx start
    send(r, rec, {0xD0, 64});      // Channel pressure
    CHECK(rec.calls.empty());
}

TEST_CASE("MIDI Learn captures the next press, CC or bend as a mapping (D30)", "[midi][router]") {
    MidiInputRouter r;
    Recorder rec;
    r.armLearn({MappingTarget::StationVelocity, StationId{4}, std::nullopt});
    CHECK(r.learning());
    std::vector<std::uint8_t> release{0x80, 40, 0};
    CHECK_FALSE(r.handle(release.data(), release.size(), "Keys", rec)); // releases don't learn
    std::vector<std::uint8_t> knob{0xB3, 71, 12};
    const auto learned = r.handle(knob.data(), knob.size(), "Keys", rec);
    REQUIRE(learned);
    CHECK(learned->source.type == MidiSourceType::ControlChange);
    CHECK(learned->source.number == 71);
    CHECK(learned->source.channel == MidiChannel::of<4>());
    CHECK(*learned->source.deviceName == "Keys");
    CHECK(learned->target == MappingTarget::StationVelocity);
    CHECK(learned->station == StationId{4});
    CHECK_FALSE(r.learning());
}
