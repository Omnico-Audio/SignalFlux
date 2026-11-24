// SignalFlux — swing and humanize (D36).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "engine/Sequencer.h"
#include "TestProjects.h"

#include <catch2/catch_test_macros.hpp>

using namespace signalflux;
using namespace signalflux::engine;
using test::contains;

namespace {

/// Two Stations, one cell apart at 1/16 grid spacing: notes every 240 ticks.
test::ProjectBuilder sixteenths() {
    test::ProjectBuilder b;
    b.settings().gridSpacing = *GridSpacing::make(240);
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62).route(1, 1, 2).route(2, 2, 1);
    b.get(1).note.duration = MusicalTick{100};
    b.get(2).note.duration = MusicalTick{100};
    return b;
}

std::vector<std::int64_t> noteOnTicks(const EventLog& log) {
    std::vector<std::int64_t> out;
    for (const auto& e : log.events())
        if (e.type == EngineEventType::NoteOn) out.push_back(e.tick.value());
    return out;
}

EventLog run(const ProjectDocument& doc, std::int64_t until, std::uint64_t seed = 1) {
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(doc).ok());
    seq.play(seed);
    seq.processUntil(MusicalTick{until});
    return log;
}

} // namespace

TEST_CASE("Swing 50% is straight", "[engine][swing]") {
    auto b = sixteenths();
    CHECK(noteOnTicks(run(b.doc(), 960)) == std::vector<std::int64_t>{0, 240, 480, 720});
}

TEST_CASE("Swing delays only the off-beat sixteenths", "[engine][swing]") {
    auto b = sixteenths();
    b.settings().swingPercent = 66; // offset = 240 * 32 / 100 = 76 ticks
    const auto log = run(b.doc(), 960);
    CHECK(noteOnTicks(log) == std::vector<std::int64_t>{0, 316, 480, 796});
    CHECK(contains(log, "416 NoteOff ch=1 n=62")); // Note Off moves with its Note On
}

TEST_CASE("Swing on eighths leaves sixteenth off-beats alone", "[engine][swing]") {
    auto b = sixteenths();
    b.settings().swingPercent = 75;
    b.settings().swingGrid = SwingGrid::Eighths; // off-beat at 480 within each 960 pair: +240
    CHECK(noteOnTicks(run(b.doc(), 960)) == std::vector<std::int64_t>{0, 240, 720, 720});
}

TEST_CASE("Swing never changes Traveler timing or routing (P1)", "[engine][swing]") {
    auto straight = sixteenths();
    auto swung = sixteenths();
    swung.settings().swingPercent = 70;
    auto arrivals = [](const EventLog& log) {
        std::vector<std::string> out;
        for (const auto& e : log.events())
            if (e.type == EngineEventType::TravelerArrive || e.type == EngineEventType::TravelerDepart)
                out.push_back(toCanonicalString(e));
        return out;
    };
    CHECK(arrivals(run(straight.doc(), 4000)) == arrivals(run(swung.doc(), 4000)));
}

TEST_CASE("Humanize velocity stays in range and is reproducible", "[engine][humanize]") {
    auto b = sixteenths();
    b.get(1).humanize.velocityRange = 20;
    const auto a = run(b.doc(), 20 * 960, 4);
    bool varied = false;
    for (const auto& e : a.events())
        if (e.type == EngineEventType::NoteOn && e.station == StationId{1}) {
            CHECK(e.velocity.value() >= 80);
            CHECK(e.velocity.value() <= 120);
            varied = varied || e.velocity.value() != 100;
        }
    CHECK(varied);
    CHECK(a.canonical() == run(b.doc(), 20 * 960, 4).canonical());
}

TEST_CASE("Humanize timing only delays, within the maximum", "[engine][humanize]") {
    auto b = sixteenths();
    b.get(2).humanize.maxDelay = MusicalTick{30};
    const auto log = run(b.doc(), 40 * 960, 9);
    std::int64_t expected = 240;
    bool delayed = false;
    for (const auto t : noteOnTicks(log)) {
        if (t % 480 == 0) continue; // Station 1 is on the beat grid and not humanized
        CHECK(t >= expected);
        CHECK(t <= expected + 30);
        delayed = delayed || t != expected;
        expected += 480;
    }
    CHECK(delayed);
}

TEST_CASE("Stop drops delayed notes that haven't started; nothing is left hanging", "[engine][humanize][transport]") {
    auto b = sixteenths();
    b.settings().swingPercent = 75; // off-beat note at 240 is delayed to 360
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{280});
    seq.requestStop(); // at 280
    seq.continuePlayback();
    seq.processUntil(MusicalTick{400});
    CHECK_FALSE(contains(log, "360 NoteOn ch=1 n=62 v=100 st=2 t=1"));
    CHECK(test::count(log, EngineEventType::NoteOn) == 1);
}
