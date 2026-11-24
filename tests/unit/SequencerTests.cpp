// SignalFlux — sequencer behavior tests (SF-003; ACCEPTANCE §2; DECISIONS D3-D6, D14, D21-D24).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Examples.h"
#include "engine/Sequencer.h"
#include "TestProjects.h"

#include <catch2/catch_test_macros.hpp>

using namespace signalflux;
using namespace signalflux::engine;
using test::contains;
using test::count;

namespace {

constexpr std::int64_t Q = kTicksPerQuarter;

/// A <-> B loop, B is 2 cells away: 1920 ticks per leg.
ProjectDocument twoStationLoop() {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {2, 0}, 64).route(1, 1, 2).route(2, 2, 1);
    return b.doc();
}

struct Rig {
    EventLog log;
    Sequencer seq{log};
    explicit Rig(const ProjectDocument& doc) { REQUIRE(seq.setDocument(doc).ok()); }
};

} // namespace

// --- Starters ---------------------------------------------------------------

TEST_CASE("Each Start Station creates one Starter on Play, in StationId order", "[engine][sequencer]") {
    test::ProjectBuilder b;
    b.station(5, {0, 0}, 60, true).station(2, {4, 0}, 62, true).station(3, {8, 0}, 64);
    Rig r{b.doc()};
    r.seq.play();
    CHECK(count(r.log, EngineEventType::TravelerSpawn) == 2);
    const auto l = test::lines(r.log);
    REQUIRE(l.size() >= 3);
    CHECK(l[0] == "0 Start");
    CHECK(l[1] == "0 Spawn t=1 st=2");
}

TEST_CASE("A Starter triggers immediately at tick 0", "[engine][sequencer]") {
    Rig r{twoStationLoop()};
    r.seq.play();
    CHECK(contains(r.log, "0 NoteOn ch=1 n=60 v=100 st=1 t=1"));
    CHECK(contains(r.log, "0 Depart t=1 st=1 fp=1 due=1920"));
}

TEST_CASE("Play without a document, or without Start Stations, does nothing harmful", "[engine][sequencer]") {
    EventLog log;
    Sequencer seq{log};
    seq.play();
    CHECK(log.events().empty());

    test::ProjectBuilder b;
    b.station(1, {0, 0});
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{10 * Q});
    CHECK(count(log, EngineEventType::NoteOn) == 0);
    CHECK(seq.now() == MusicalTick{10 * Q});
}

// --- Leg timing ---------------------------------------------------------------

TEST_CASE("Leg duration = Manhattan distance x grid spacing", "[engine][sequencer]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {3, 2}, 64);
    b.route(1, 1, 2);
    SECTION("default spacing: 5 cells = 4800 ticks") {
        Rig r{b.doc()};
        r.seq.play();
        r.seq.processUntil(MusicalTick{10 * Q});
        CHECK(contains(r.log, "4800 Arrive t=1 st=2"));
    }
    SECTION("1/8 spacing halves it") {
        b.settings().gridSpacing = *GridSpacing::make(480);
        Rig r{b.doc()};
        r.seq.play();
        r.seq.processUntil(MusicalTick{10 * Q});
        CHECK(contains(r.log, "2400 Arrive t=1 st=2"));
    }
}

TEST_CASE("processUntil is half-open: events at the horizon wait for the next call", "[engine][sequencer]") {
    Rig r{twoStationLoop()};
    r.seq.play();
    r.seq.processUntil(MusicalTick{1920});
    CHECK_FALSE(contains(r.log, "1920 Arrive t=1 st=2"));
    CHECK(r.seq.now() == MusicalTick{1920});
    r.seq.processUntil(MusicalTick{1921});
    CHECK(contains(r.log, "1920 Arrive t=1 st=2"));
}

TEST_CASE("Event timing is independent of how processUntil is chunked (UI frame rate)", "[engine][sequencer][determinism]") {
    const auto doc = *examples::make("instant-bloom");
    const MusicalTick end{32 * Q};

    auto runWithChunks = [&](auto nextChunk) {
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(doc).ok());
        seq.play();
        while (seq.now() < end) seq.processUntil(std::min(end, seq.now() + MusicalTick{nextChunk()}));
        return log.canonical();
    };

    const auto oneShot = runWithChunks([&] { return end.value(); });
    const auto everyTick = runWithChunks([] { return std::int64_t{1}; });
    const auto sixtyFps = runWithChunks([] { return std::int64_t{16}; }); // ~1/60 s at 120 BPM
    std::int64_t jitter = 0;
    const auto irregular = runWithChunks([&] { jitter = (jitter * 37 + 11) % 97; return jitter + 1; });

    CHECK(everyTick == oneShot);
    CHECK(sixtyFps == oneShot);
    CHECK(irregular == oneShot);
}

// --- Branching (D6) -----------------------------------------------------------

TEST_CASE("Sequential branching round-robins in outgoing order and resets on Play", "[engine][sequencer]") {
    Rig r{*examples::make("sequential-hub")};
    auto departuresFromHub = [&] {
        std::vector<std::uint32_t> fps;
        for (const auto& e : r.log.events())
            if (e.type == EngineEventType::TravelerDepart && e.station == StationId{1}) fps.push_back(e.flowpoint.value());
        return fps;
    };
    r.seq.play();
    r.seq.processUntil(MusicalTick{20 * Q});
    const auto first = departuresFromHub();
    REQUIRE(first.size() >= 4);
    CHECK(std::vector<std::uint32_t>(first.begin(), first.begin() + 4) == std::vector<std::uint32_t>{20, 30, 40, 20});

    r.log.clear();
    r.seq.play();
    r.seq.processUntil(MusicalTick{1});
    CHECK(departuresFromHub() == std::vector<std::uint32_t>{20});
}

TEST_CASE("Instant branching departs on every route at the same tick; children numbered in outgoing order",
          "[engine][sequencer]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}).station(3, {0, 1}).station(4, {0, 2});
    b.route(10, 1, 2).route(11, 1, 3).route(12, 1, 4);
    b.get(1).branchMode = BranchMode::Instant;
    Rig r{b.doc()};
    r.seq.play();
    CHECK(contains(r.log, "0 Depart t=1 st=1 fp=10 due=960"));
    CHECK(contains(r.log, "0 Depart t=2 st=1 fp=11 due=960"));
    CHECK(contains(r.log, "0 Depart t=3 st=1 fp=12 due=1920"));
}

TEST_CASE("Simultaneous arrivals are processed in TravelerId order (D21)", "[engine][sequencer]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62).station(3, {0, 1}, 64);
    b.route(10, 1, 2).route(11, 1, 3);
    b.get(1).branchMode = BranchMode::Instant;
    Rig r{b.doc()};
    r.seq.play();
    r.seq.processUntil(MusicalTick{961});
    const auto l = test::lines(r.log);
    const auto a1 = std::find(l.begin(), l.end(), "960 Arrive t=1 st=2");
    const auto a2 = std::find(l.begin(), l.end(), "960 Arrive t=2 st=3");
    REQUIRE(a1 != l.end());
    REQUIRE(a2 != l.end());
    CHECK(a1 < a2);
}

// --- Caps (D23) -------------------------------------------------------------------

TEST_CASE("Traveler cap: default 8 drops excess Instant children deterministically", "[engine][sequencer]") {
    Rig r{*examples::make("instant-bloom")};
    r.seq.play();
    r.seq.processUntil(MusicalTick{40 * Q});
    CHECK(r.seq.stats().travelerHighWater == 8);
    CHECK(r.seq.stats().droppedTravelers > 0);
    CHECK(count(r.log, EngineEventType::TravelerDropped) == r.seq.stats().droppedTravelers);
    CHECK(r.seq.snapshot().travelers.size() <= 8);
}

TEST_CASE("Traveler cap of 256 lets the same patch grow further", "[engine][sequencer]") {
    auto doc = *examples::make("instant-bloom");
    doc.settings.travelerCap = 256;
    Rig r{doc};
    r.seq.play();
    r.seq.processUntil(MusicalTick{40 * Q});
    CHECK(r.seq.stats().travelerHighWater > 8);
    CHECK(r.seq.stats().travelerHighWater <= 256);
}

TEST_CASE("Starters count toward the Traveler cap, lowest StationId first", "[engine][sequencer]") {
    test::ProjectBuilder b;
    b.settings().travelerCap = 2;
    b.station(3, {0, 0}, 60, true).station(1, {2, 0}, 60, true).station(2, {4, 0}, 60, true).station(9, {6, 0});
    b.route(1, 3, 9).route(2, 1, 9).route(3, 2, 9);
    Rig r{b.doc()};
    r.seq.play();
    CHECK(contains(r.log, "0 Spawn t=1 st=1"));
    CHECK(contains(r.log, "0 Spawn t=2 st=2"));
    CHECK(contains(r.log, "0 TravelerDropped t=0 st=3"));
}

TEST_CASE("Active-note cap drops Note Ons, never Note Offs (D23)", "[engine][sequencer]") {
    test::ProjectBuilder b;
    b.settings().activeNoteCap = 1;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62, true);
    Rig r{b.doc()};
    r.seq.play();
    r.seq.processUntil(MusicalTick{2 * Q});
    CHECK(count(r.log, EngineEventType::NoteOn) == 1);
    CHECK(count(r.log, EngineEventType::NoteDropped) == 1);
    CHECK(count(r.log, EngineEventType::NoteOff) == 1);
    CHECK(r.seq.stats().droppedNotes == 1);
}

// --- Note lifecycle (D20, D22) ------------------------------------------------------

TEST_CASE("Every Note On gets its Note Off at the Station's own duration (D20)", "[engine][sequencer]") {
    Rig r{twoStationLoop()};
    r.seq.play();
    r.seq.processUntil(MusicalTick{8 * Q});
    CHECK(contains(r.log, "480 NoteOff ch=1 n=60"));
    CHECK(contains(r.log, "2400 NoteOff ch=1 n=64"));
    CHECK(count(r.log, EngineEventType::NoteOn) == count(r.log, EngineEventType::NoteOff));
}

TEST_CASE("Note Off precedes Note On at the same tick for the same pitch (D21)", "[engine][sequencer]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 60).route(1, 1, 2).route(2, 2, 1);
    b.get(1).note.duration = MusicalTick{Q};
    b.get(2).note.duration = MusicalTick{Q};
    Rig r{b.doc()};
    r.seq.play();
    r.seq.processUntil(MusicalTick{Q + 1});
    const auto l = test::lines(r.log);
    const auto off = std::find(l.begin(), l.end(), "960 NoteOff ch=1 n=60");
    const auto on = std::find(l.begin(), l.end(), "960 NoteOn ch=1 n=60 v=100 st=2 t=1");
    REQUIRE(off != l.end());
    REQUIRE(on != l.end());
    CHECK(off < on);
}

TEST_CASE("Overlapping same channel+pitch: both Note Ons sent, one Note Off at the last end (D22)",
          "[engine][sequencer]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 60).route(1, 1, 2);
    b.get(1).note.duration = MusicalTick{2 * Q}; // 0..1920
    b.get(2).note.duration = MusicalTick{2 * Q}; // 960..2880
    Rig r{b.doc()};
    r.seq.play();
    r.seq.processUntil(MusicalTick{4 * Q});
    CHECK(count(r.log, EngineEventType::NoteOn) == 2);
    CHECK(count(r.log, EngineEventType::NoteOff) == 1);
    CHECK(contains(r.log, "2880 NoteOff ch=1 n=60"));
}

TEST_CASE("Changing a Station's channel mid-note releases on the original channel (D22)", "[engine][sequencer]") {
    auto doc = twoStationLoop();
    Rig r{doc};
    r.seq.play();
    r.seq.processUntil(MusicalTick{100});
    test::stationIn(doc, 1).channel = MidiChannel::of<5>();
    REQUIRE(r.seq.setDocument(doc).ok());
    r.seq.processUntil(MusicalTick{1000});
    CHECK(contains(r.log, "480 NoteOff ch=1 n=60"));
}

TEST_CASE("A muted Station is silent but still routes the Traveler (P2)", "[engine][sequencer]") {
    auto doc = twoStationLoop();
    test::stationIn(doc, 2).manuallyMuted = true;
    Rig r{doc};
    r.seq.play();
    r.seq.processUntil(MusicalTick{4000});
    CHECK(contains(r.log, "1920 Arrive t=1 st=2"));
    CHECK(contains(r.log, "1920 Depart t=1 st=2 fp=2 due=3840"));
    CHECK_FALSE(contains(r.log, "1920 NoteOn ch=1 n=64 v=100 st=2 t=1"));
    CHECK(contains(r.log, "3840 NoteOn ch=1 n=60 v=100 st=1 t=1"));
}

TEST_CASE("A Station with no outgoing route ends the chain; the transport keeps running (D24)",
          "[engine][sequencer]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62).route(1, 1, 2);
    Rig r{b.doc()};
    r.seq.play();
    r.seq.processUntil(MusicalTick{4 * Q});
    CHECK(contains(r.log, "960 End t=1 st=2 why=no-route"));
    CHECK(r.seq.transport() == TransportState::Playing);
    CHECK(r.seq.now() == MusicalTick{4 * Q});
    CHECK(r.seq.snapshot().travelers.empty());
}

// --- In-flight edits (D3, D4, D5) -----------------------------------------------------

TEST_CASE("Moving the destination mid-flight keeps elapsed ticks (D3)", "[engine][sequencer][edits]") {
    auto doc = twoStationLoop(); // leg 1 -> 2: 0..1920
    Rig r{doc};
    r.seq.play();
    r.seq.processUntil(MusicalTick{1000});

    SECTION("farther: arrival later by exactly the added cells") {
        test::stationIn(doc, 2).position = {4, 0}; // 4 cells = 3840
        REQUIRE(r.seq.setDocument(doc).ok());
        r.seq.processUntil(MusicalTick{8 * Q});
        CHECK(contains(r.log, "3840 Arrive t=1 st=2"));
        CHECK_FALSE(contains(r.log, "1920 Arrive t=1 st=2"));
    }
    SECTION("closer than already travelled: arrives at the edit tick") {
        test::stationIn(doc, 2).position = {1, 0}; // 960 < 1000 elapsed
        REQUIRE(r.seq.setDocument(doc).ok());
        r.seq.processUntil(MusicalTick{1001});
        CHECK(contains(r.log, "1000 Arrive t=1 st=2"));
    }
    SECTION("edit applied exactly at the old deadline arrives once") {
        r.seq.processUntil(MusicalTick{1920});
        test::stationIn(doc, 2).position = {2, 1}; // 3 cells = 2880, so the 1920 arrival moves later
        REQUIRE(r.seq.setDocument(doc).ok());
        r.seq.processUntil(MusicalTick{6 * Q});
        CHECK(count(r.log, EngineEventType::TravelerArrive) >= 1);
        CHECK_FALSE(contains(r.log, "1920 Arrive t=1 st=2"));
        CHECK(contains(r.log, "2880 Arrive t=1 st=2"));
    }
}

TEST_CASE("Moving the source mid-flight uses the same rule (D4)", "[engine][sequencer][edits]") {
    auto doc = twoStationLoop();
    Rig r{doc};
    r.seq.play();
    r.seq.processUntil(MusicalTick{500});
    test::stationIn(doc, 1).position = {0, 1}; // 1 -> 2 becomes 3 cells = 2880
    REQUIRE(r.seq.setDocument(doc).ok());
    r.seq.processUntil(MusicalTick{4 * Q});
    CHECK(contains(r.log, "2880 Arrive t=1 st=2"));
}

TEST_CASE("Changing grid spacing mid-flight follows the same rule", "[engine][sequencer][edits]") {
    auto doc = twoStationLoop();
    Rig r{doc};
    r.seq.play();
    r.seq.processUntil(MusicalTick{500});
    doc.settings.gridSpacing = *GridSpacing::make(1920); // 2 cells = 3840
    REQUIRE(r.seq.setDocument(doc).ok());
    r.seq.processUntil(MusicalTick{5 * Q});
    CHECK(contains(r.log, "3840 Arrive t=1 st=2"));
}

TEST_CASE("Deleting the route under a Traveler ends it; its note still ends on schedule (D5)",
          "[engine][sequencer][edits]") {
    auto doc = twoStationLoop();
    Rig r{doc};
    r.seq.play();
    r.seq.processUntil(MusicalTick{100});
    test::removeFlowpoint(doc, 1);
    REQUIRE(r.seq.setDocument(doc).ok());
    r.seq.processUntil(MusicalTick{4 * Q});
    CHECK(contains(r.log, "100 End t=1 st=2 why=route-deleted"));
    CHECK(contains(r.log, "480 NoteOff ch=1 n=60"));
    CHECK_FALSE(contains(r.log, "1920 Arrive t=1 st=2"));
    CHECK(r.seq.snapshot().travelers.empty());
}

TEST_CASE("Deleting the destination Station ends the Traveler with its own reason", "[engine][sequencer][edits]") {
    auto doc = twoStationLoop();
    Rig r{doc};
    r.seq.play();
    r.seq.processUntil(MusicalTick{100});
    test::removeStation(doc, 2);
    REQUIRE(r.seq.setDocument(doc).ok());
    CHECK(contains(r.log, "100 End t=1 st=2 why=station-deleted"));
}

TEST_CASE("Recreating a deleted route does not revive the Traveler (D5)", "[engine][sequencer][edits]") {
    auto doc = twoStationLoop();
    const auto original = doc;
    Rig r{doc};
    r.seq.play();
    r.seq.processUntil(MusicalTick{100});
    test::removeFlowpoint(doc, 1);
    REQUIRE(r.seq.setDocument(doc).ok());
    REQUIRE(r.seq.setDocument(original).ok());
    r.seq.processUntil(MusicalTick{4 * Q});
    CHECK(count(r.log, EngineEventType::TravelerArrive) == 0);
}

TEST_CASE("An invalid edit is rejected and changes nothing", "[engine][sequencer][edits]") {
    auto doc = twoStationLoop();
    Rig r{doc};
    r.seq.play();
    r.seq.processUntil(MusicalTick{100});
    test::stationIn(doc, 2).position = {0, 0}; // collides with Station 1
    const auto report = r.seq.setDocument(doc);
    CHECK_FALSE(report.ok());
    r.seq.processUntil(MusicalTick{2000});
    CHECK(contains(r.log, "1920 Arrive t=1 st=2"));
}

// --- Transport (D14) ----------------------------------------------------------------

TEST_CASE("Stop takes effect at the next MIDI Clock boundary and releases notes", "[engine][sequencer][transport]") {
    Rig r{twoStationLoop()};
    r.seq.play();
    r.seq.processUntil(MusicalTick{100});
    r.seq.requestStop();
    REQUIRE(r.seq.pendingStop() == MusicalTick{120});
    CHECK(r.seq.transport() == TransportState::Playing);
    r.seq.processUntil(MusicalTick{5000}); // the host keeps asking; the engine stops at 120
    CHECK(r.seq.transport() == TransportState::Paused);
    CHECK(r.seq.now() == MusicalTick{120});
    CHECK(contains(r.log, "120 NoteOff ch=1 n=60"));
    CHECK(contains(r.log, "120 Stop"));
    CHECK_FALSE(contains(r.log, "480 NoteOff ch=1 n=60")); // already released at Stop
}

TEST_CASE("Stop on a clock boundary is immediate", "[engine][sequencer][transport]") {
    Rig r{twoStationLoop()};
    r.seq.play();
    r.seq.processUntil(MusicalTick{160});
    r.seq.requestStop();
    CHECK(r.seq.transport() == TransportState::Paused);
    CHECK(contains(r.log, "160 Stop"));
}

TEST_CASE("Continue resumes frozen Travelers without retriggering cut notes", "[engine][sequencer][transport]") {
    Rig r{twoStationLoop()};
    r.seq.play();
    r.seq.processUntil(MusicalTick{210});
    r.seq.requestStop(); // next clock boundary: 240
    r.seq.processUntil(MusicalTick{240});
    const auto frozen = r.seq.snapshot();
    REQUIRE(frozen.travelers.size() == 1);

    r.seq.processUntil(MusicalTick{1000}); // stopped: time does not advance
    CHECK(r.seq.now() == MusicalTick{240});

    r.seq.continuePlayback();
    CHECK(contains(r.log, "240 Continue"));
    CHECK(r.seq.snapshot().travelers.front().deadline == frozen.travelers.front().deadline);
    r.seq.processUntil(MusicalTick{2000});
    CHECK(contains(r.log, "1920 Arrive t=1 st=2"));
    CHECK(count(r.log, EngineEventType::NoteOn) == 2); // tick-0 note and the 1920 arrival, nothing re-sent
}

TEST_CASE("Play resets runtime state; Play while playing releases notes first", "[engine][sequencer][transport]") {
    Rig r{twoStationLoop()};
    r.seq.play();
    r.seq.processUntil(MusicalTick{100});
    r.seq.play();
    const auto l = test::lines(r.log);
    const auto off = std::find(l.begin(), l.end(), "100 NoteOff ch=1 n=60");
    REQUIRE(off != l.end());
    CHECK(*(off + 1) == "0 Start");
    CHECK(r.seq.now() == MusicalTick{0});
    CHECK(contains(r.log, "0 Spawn t=1 st=1"));
}

TEST_CASE("Panic releases every sounding note and leaves the transport running", "[engine][sequencer][transport]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62, true);
    b.get(2).channel = MidiChannel::of<9>();
    Rig r{b.doc()};
    r.seq.play();
    r.seq.processUntil(MusicalTick{10});
    r.seq.panic();
    CHECK(contains(r.log, "10 NoteOff ch=1 n=60"));
    CHECK(contains(r.log, "10 NoteOff ch=9 n=62"));
    CHECK(contains(r.log, "10 Panic"));
    CHECK(r.seq.transport() == TransportState::Playing);
    r.seq.processUntil(MusicalTick{1000});
    CHECK(count(r.log, EngineEventType::NoteOff) == 2); // nothing left to release later
}

// --- Snapshot for the UI -------------------------------------------------------------

TEST_CASE("Snapshot exposes Traveler legs for interpolation, sorted by id", "[engine][sequencer][snapshot]") {
    Rig r{*examples::make("instant-bloom")};
    r.seq.play();
    r.seq.processUntil(MusicalTick{100});
    const auto snap = r.seq.snapshot();
    REQUIRE(snap.travelers.size() == 2);
    CHECK(snap.travelers[0].id == TravelerId{1});
    CHECK(snap.travelers[0].from == StationId{1});
    CHECK(snap.travelers[0].legStart == MusicalTick{0});
    CHECK(snap.travelers[0].deadline == MusicalTick{1920});
    CHECK(snap.travelers[1].id == TravelerId{2});
    CHECK(snap.now == MusicalTick{100});
}
