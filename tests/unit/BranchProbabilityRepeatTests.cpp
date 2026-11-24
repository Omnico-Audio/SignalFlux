// SignalFlux — weighted Random branching, probability, Repeat (SF-005; D7, D8, D32).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Examples.h"
#include "engine/Sequencer.h"
#include "TestProjects.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace signalflux;
using namespace signalflux::engine;
using test::contains;
using test::count;

namespace {

constexpr std::int64_t Q = kTicksPerQuarter;

/// Hub (Station 1, Random) with two spokes (2 and 3) one cell away, both returning.
test::ProjectBuilder randomHub(int weight2, int weight3) {
    test::ProjectBuilder b;
    b.station(1, {1, 1}, 60, true).station(2, {2, 1}, 62).station(3, {1, 2}, 64);
    b.route(10, 1, 2).route(11, 1, 3).route(20, 2, 1).route(30, 3, 1);
    b.get(1).branchMode = BranchMode::Random;
    b.doc().plots.front().flowpoints[0].weight = *RouteWeight::make(weight2);
    b.doc().plots.front().flowpoints[1].weight = *RouteWeight::make(weight3);
    return b;
}

std::map<std::uint32_t, int> hubChoices(const EventLog& log) {
    std::map<std::uint32_t, int> n;
    for (const auto& e : log.events())
        if (e.type == EngineEventType::TravelerDepart && e.station == StationId{1}) ++n[e.flowpoint.value()];
    return n;
}

std::vector<std::int64_t> noteOnTicks(const EventLog& log, std::uint32_t station) {
    std::vector<std::int64_t> ticks;
    for (const auto& e : log.events())
        if (e.type == EngineEventType::NoteOn && e.station == StationId{station}) ticks.push_back(e.tick.value());
    return ticks;
}

} // namespace

// --- Weighted Random branching (D32) --------------------------------------------------

TEST_CASE("Random branching follows the weights", "[engine][branching]") {
    auto b = randomHub(3, 1);
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play(42);
    seq.processUntil(MusicalTick{4000 * Q});
    const auto n = hubChoices(log);
    const double total = n.at(10) + n.at(11);
    REQUIRE(total > 1500);
    const double share = n.at(10) / total;
    CHECK(share > 0.70);
    CHECK(share < 0.80);
}

TEST_CASE("Weight 0 is never chosen", "[engine][branching]") {
    auto b = randomHub(0, 5);
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play(1);
    seq.processUntil(MusicalTick{400 * Q});
    const auto n = hubChoices(log);
    CHECK(n.count(10) == 0);
    CHECK(n.at(11) > 100);
}

TEST_CASE("All weights 0: the Traveler ends at the Station (D32)", "[engine][branching]") {
    auto b = randomHub(0, 0);
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    CHECK(contains(log, "0 End t=1 st=1 why=no-route"));
}

TEST_CASE("Random branching is reproducible for a seed and differs across seeds", "[engine][branching]") {
    auto b = randomHub(1, 1);
    auto run = [&](std::uint64_t seed) {
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(b.doc()).ok());
        seq.play(seed);
        seq.processUntil(MusicalTick{200 * Q});
        return log.canonical();
    };
    CHECK(run(9) == run(9));
    CHECK(run(9) != run(10));
}

// --- Probability (D7) -----------------------------------------------------------------

TEST_CASE("Probability 0 never sounds, 100 always sounds", "[engine][probability]") {
    for (const int p : {0, 100}) {
        test::ProjectBuilder b;
        b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62).route(1, 1, 2).route(2, 2, 1);
        b.get(2).probability = *ProbabilityPercent::make(p);
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(b.doc()).ok());
        seq.play();
        seq.processUntil(MusicalTick{200 * Q});
        CHECK(noteOnTicks(log, 2).size() == (p == 0 ? 0u : 100u));
        CHECK(count(log, EngineEventType::TravelerArrive) == 199); // routing is unaffected either way
    }
}

TEST_CASE("A failed probability check is a silent pass-through: same timing and routing (D7)",
          "[engine][probability]") {
    auto build = [](int p) {
        test::ProjectBuilder b;
        b.station(1, {0, 0}, 60, true).station(2, {2, 0}, 62).station(3, {2, 1}, 64);
        b.route(1, 1, 2).route(2, 2, 3).route(3, 3, 1);
        b.get(2).probability = *ProbabilityPercent::make(p);
        b.get(3).note.mode = NoteMode::Relative; // follows Station 2's resolved note even when it is silent
        b.get(3).note.relativeSteps = 1;
        return b.doc();
    };
    auto run = [](const ProjectDocument& doc) {
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(doc).ok());
        seq.play(5);
        seq.processUntil(MusicalTick{100 * Q});
        return log;
    };
    const auto always = run(build(100));
    const auto half = run(build(50));

    // Every event except Station 2's notes is identical.
    auto withoutStation2Notes = [](const EventLog& log) {
        std::vector<std::string> out;
        for (const auto& e : log.events()) {
            const bool st2Note = (e.type == EngineEventType::NoteOn && e.station == StationId{2}) ||
                                 (e.type == EngineEventType::NoteOff && e.note.value() == 62);
            if (!st2Note) out.push_back(toCanonicalString(e));
        }
        return out;
    };
    CHECK(withoutStation2Notes(half) == withoutStation2Notes(always));

    const auto fired = noteOnTicks(half, 2).size();
    const auto arrivals = noteOnTicks(always, 2).size();
    CHECK(fired > arrivals / 4);
    CHECK(fired < arrivals * 3 / 4);
}

TEST_CASE("Probability is reproducible for a seed", "[engine][probability]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62).route(1, 1, 2).route(2, 2, 1);
    b.get(2).probability = *ProbabilityPercent::make(30);
    auto run = [&](std::uint64_t seed) {
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(b.doc()).ok());
        seq.play(seed);
        seq.processUntil(MusicalTick{100 * Q});
        return noteOnTicks(log, 2);
    };
    CHECK(run(77) == run(77));
}

// --- Repeat (D8) -----------------------------------------------------------------------

TEST_CASE("additionalRepeats = 2 plays 3 times, then departs with the final trigger", "[engine][repeat]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62).route(1, 1, 2);
    b.get(1).additionalRepeats = 2;
    b.get(1).repeatInterval = MusicalTick{240};
    b.get(1).note.duration = MusicalTick{120};
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{4 * Q});

    CHECK(noteOnTicks(log, 1) == std::vector<std::int64_t>{0, 240, 480});
    CHECK(contains(log, "120 NoteOff ch=1 n=60"));
    CHECK(contains(log, "360 NoteOff ch=1 n=60"));
    CHECK(contains(log, "600 NoteOff ch=1 n=60"));
    CHECK(contains(log, "480 Depart t=1 st=1 fp=1 due=1440"));
    CHECK(count(log, EngineEventType::TravelerRepeat) == 2);
}

TEST_CASE("Repeat interval defaults to the note duration", "[engine][repeat]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true);
    b.get(1).additionalRepeats = 1;
    b.get(1).note.duration = MusicalTick{300};
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{2 * Q});
    CHECK(noteOnTicks(log, 1) == std::vector<std::int64_t>{0, 300});
    // Note Off of the first and Note On of the repeat share tick 300: Off comes first (D21).
    const auto l = test::lines(log);
    const auto off = std::find(l.begin(), l.end(), "300 NoteOff ch=1 n=60");
    const auto on = std::find(l.begin(), l.end(), "300 NoteOn ch=1 n=60 v=100 st=1 t=1");
    REQUIRE(off != l.end());
    REQUIRE(on != l.end());
    CHECK(off < on);
}

TEST_CASE("Repeats replay the same resolved note, even for random Stations", "[engine][repeat]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true);
    b.get(1).additionalRepeats = 3;
    b.get(1).note.mode = NoteMode::RandomValue;
    b.get(1).note.random.rangeSteps = 7;
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play(3);
    seq.processUntil(MusicalTick{4 * Q});
    std::vector<int> notes;
    for (const auto& e : log.events())
        if (e.type == EngineEventType::NoteOn) notes.push_back(e.note.value());
    REQUIRE(notes.size() == 4);
    CHECK(std::all_of(notes.begin(), notes.end(), [&](int n) { return n == notes.front(); }));
}

TEST_CASE("A silent arrival keeps its repeat timing (P2, D7)", "[engine][repeat]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62).route(1, 1, 2);
    b.get(1).additionalRepeats = 2;
    b.get(1).manuallyMuted = true;
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{4 * Q});
    CHECK(noteOnTicks(log, 1).empty());
    CHECK(contains(log, "960 Depart t=1 st=1 fp=1 due=1920")); // 2 x 480 default interval
}

TEST_CASE("Stop during a Repeat group: no further Note Ons and no stuck notes (D8)", "[engine][repeat][transport]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62).route(1, 1, 2);
    b.get(1).additionalRepeats = 4;
    b.get(1).repeatInterval = MusicalTick{240};
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{300});
    seq.requestStop(); // at 320
    seq.processUntil(MusicalTick{320});
    seq.continuePlayback();
    seq.processUntil(MusicalTick{4 * Q});
    CHECK(noteOnTicks(log, 1) == std::vector<std::int64_t>{0, 240});
    CHECK(contains(log, "320 NoteOff ch=1 n=60"));
    CHECK(contains(log, "960 Depart t=1 st=1 fp=1 due=1920")); // dwell time unchanged
    CHECK_FALSE(contains(log, "480 NoteOff ch=1 n=60")); // already released at Stop
    CHECK(contains(log, "2400 NoteOff ch=1 n=62"));
    CHECK(count(log, EngineEventType::NoteOff) == 2);
}

TEST_CASE("Panic during a Repeat group silences the rest of it", "[engine][repeat]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true);
    b.get(1).additionalRepeats = 4;
    b.get(1).repeatInterval = MusicalTick{240};
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{250});
    seq.panic();
    seq.processUntil(MusicalTick{4 * Q});
    CHECK(noteOnTicks(log, 1) == std::vector<std::int64_t>{0, 240});
}

TEST_CASE("Deleting the Station a Traveler is dwelling at ends it", "[engine][repeat][edits]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62, true);
    b.get(2).additionalRepeats = 4;
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{100});
    auto doc = b.doc();
    test::removeStation(doc, 2);
    REQUIRE(seq.setDocument(doc).ok());
    CHECK(contains(log, "100 End t=2 st=2 why=station-deleted"));
    CHECK(seq.snapshot().travelers.empty());
}

TEST_CASE("Snapshot marks dwelling Travelers", "[engine][repeat][snapshot]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true);
    b.get(1).additionalRepeats = 2;
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    const auto snap = seq.snapshot();
    REQUIRE(snap.travelers.size() == 1);
    CHECK(snap.travelers[0].dwelling);
    CHECK(snap.travelers[0].from == StationId{1});
    CHECK(snap.travelers[0].deadline == MusicalTick{480});
}
