// SignalFlux — edits, undo/redo, route geometry, MIDI file export (SF-009; D27, D28, D31, D35).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Editing.h"
#include "domain/Examples.h"
#include "domain/RouteGeometry.h"
#include "domain/Validation.h"
#include "engine/EventLog.h"
#include "engine/Sequencer.h"
#include "midi/MidiFileWriter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace signalflux;
using namespace signalflux::editing;

TEST_CASE("Building a patch from scratch keeps the document valid at every step", "[editing]") {
    auto doc = makeDefaultProject();
    const auto a = addStation(doc, {1, 1});
    const auto b = addStation(doc, {4, 1});
    const auto c = addStation(doc, {4, 3});
    REQUIRE((a && b && c));
    CHECK(findStation(doc, *a)->isStart); // first Station starts
    CHECK_FALSE(findStation(doc, *b)->isStart);
    CHECK_FALSE(addStation(doc, {1, 1}));  // occupied
    CHECK_FALSE(addStation(doc, {64, 0})); // outside
    REQUIRE(connect(doc, *a, *b));
    REQUIRE(connect(doc, *b, *c));
    REQUIRE(connect(doc, *c, *a));
    CHECK_FALSE(connect(doc, *a, *a)); // self
    CHECK_FALSE(connect(doc, *a, *b)); // duplicate
    CHECK(validateProject(doc).ok());
    CHECK(findStation(doc, *a)->colour != findStation(doc, *b)->colour);
}

TEST_CASE("Deleting a Station removes every reference to it", "[editing]") {
    auto doc = *examples::make("gatekeeper");
    auto& p = doc.plots.front();
    doc.scenes.push_back(Scene{SceneId{1}, "s", {StationId{2}}, {}, std::nullopt});
    MidiMapping m;
    m.id = MappingId{1};
    m.target = MappingTarget::StationVelocity;
    m.station = StationId{2};
    doc.midi.mappings.push_back(m);
    REQUIRE(deleteStation(doc, StationId{2}));
    CHECK(validateProject(doc).ok());
    CHECK(std::none_of(p.flowpoints.begin(), p.flowpoints.end(),
                       [](const Flowpoint& f) { return f.from == StationId{2} || f.to == StationId{2}; }));
    CHECK(doc.scenes.front().mutedStations.empty());
    CHECK(doc.midi.mappings.empty());
    REQUIRE(deleteStation(doc, StationId{1}));
    CHECK(validateProject(doc).ok()); // gate input "Station Active 1" went with it
}

TEST_CASE("Deleting gates and switches removes inputs that referred to them", "[editing]") {
    auto doc = *examples::make("gatekeeper");
    REQUIRE(deleteSwitch(doc, SwitchId{1}));
    CHECK(validateProject(doc).ok());
    REQUIRE(deleteGate(doc, GateId{3})); // the blinker feeds itself
    CHECK(validateProject(doc).ok());
}

TEST_CASE("Moving Stations respects occupancy and bounds", "[editing]") {
    auto doc = *examples::make("first-loop");
    CHECK(moveStation(doc, StationId{1}, {5, 5}));
    CHECK_FALSE(moveStation(doc, StationId{1}, {2, 0})); // Station 2 is there
    CHECK_FALSE(moveStation(doc, StationId{1}, {-1, 0}));
    CHECK(findStation(doc, StationId{1})->position == GridCoordinate{5, 5});
}

TEST_CASE("Undo and redo restore exact documents; a new edit clears redo", "[editing][undo]") {
    auto doc = makeDefaultProject();
    History h;
    h.reset(doc);
    addStation(doc, {1, 1});
    h.commit(doc, "Add Station");
    addStation(doc, {2, 2});
    h.commit(doc, "Add Station");
    CHECK(h.undoLabel() == "Add Station");
    CHECK(h.undo().plots.front().stations.size() == 1);
    CHECK(h.undo().plots.front().stations.empty());
    CHECK_FALSE(h.canUndo());
    CHECK(h.redo().plots.front().stations.size() == 1);
    auto branch = h.current();
    addStation(branch, {7, 7});
    h.commit(branch, "Add Station");
    CHECK_FALSE(h.canRedo());
    CHECK(h.current() == branch);
}

TEST_CASE("A drag is one undo step (gesture coalescing)", "[editing][undo]") {
    auto doc = *examples::make("first-loop");
    History h;
    h.reset(doc);
    for (int x = 3; x <= 9; ++x) {
        moveStation(doc, StationId{2}, {x, 0});
        h.commit(doc, "Move Station", "drag-2");
    }
    h.endGesture();
    moveStation(doc, StationId{2}, {9, 3});
    h.commit(doc, "Move Station", "drag-2");
    CHECK(h.size() == 3);
    CHECK(h.undo().plots.front().stations[1].position == GridCoordinate{9, 0});
    CHECK(h.undo().plots.front().stations[1].position == GridCoordinate{2, 0});
}

TEST_CASE("History is bounded", "[editing][undo]") {
    auto doc = makeDefaultProject();
    History h{10};
    h.reset(doc);
    for (int i = 0; i < 30; ++i) {
        doc.metadata.notes = std::to_string(i);
        h.commit(doc, "Edit notes");
    }
    CHECK(h.size() == 11);
    while (h.canUndo()) (void)h.undo();
    CHECK(h.current().metadata.notes == "19");
}

TEST_CASE("Scenes store and recall mutes, Switches and tempo (D35)", "[editing][scenes]") {
    auto doc = *examples::make("gatekeeper");
    auto& p = doc.plots.front();
    p.stations[0].manuallyMuted = true;
    p.switches[0].on = true;
    doc.settings.tempo = *MilliBpm::make(90'000);
    const auto id = storeScene(doc, std::nullopt, "A");
    p.stations[0].manuallyMuted = false;
    p.switches[0].on = false;
    doc.settings.tempo = MilliBpm{};
    const auto recalled = withSceneApplied(doc, id);
    CHECK(recalled.plots.front().stations[0].manuallyMuted);
    CHECK(recalled.plots.front().switches[0].on);
    CHECK(recalled.settings.tempo.value() == 90'000);
    CHECK(validateProject(recalled).ok());
    CHECK(storeScene(doc, id, "A2") == id); // overwrite in place
    CHECK(doc.scenes.size() == 1);
}

TEST_CASE("Learned mappings replace mappings with the same source (D30)", "[editing]") {
    auto doc = *examples::make("first-loop");
    MidiMapping m;
    m.source = {std::string("Pad"), MidiChannel::of<1>(), MidiSourceType::ControlChange, 20};
    m.target = MappingTarget::Tempo;
    addMapping(doc, m);
    m.target = MappingTarget::GlobalTranspose;
    addMapping(doc, m);
    REQUIRE(doc.midi.mappings.size() == 1);
    CHECK(doc.midi.mappings[0].target == MappingTarget::GlobalTranspose);
    CHECK(validateProject(doc).ok());
}

// --- Route geometry and beat ticks (D2, D31) ------------------------------------------------

TEST_CASE("Routes are orthogonal with one elbow; points along them are exact", "[geometry]") {
    const auto r = geometry::routeBetween({0, 0}, {3, 2}, RouteElbow::HorizontalFirst);
    CHECK(r.length == 5.0);
    CHECK(r.points[1].x == 3.0);
    CHECK(r.points[1].y == 0.0);
    const auto mid = geometry::pointAlong(r, 0.6); // 3 cells: exactly the corner
    CHECK(mid.x == Catch::Approx(3.0));
    CHECK(mid.y == Catch::Approx(0.0));
    const auto later = geometry::pointAlong(r, 0.8);
    CHECK(later.x == Catch::Approx(3.0));
    CHECK(later.y == Catch::Approx(1.0));
    const auto v = geometry::routeBetween({0, 0}, {3, 2}, RouteElbow::VerticalFirst);
    CHECK(v.points[1].x == 0.0);
    CHECK(v.points[1].y == 2.0);
}

TEST_CASE("Beat ticks mark real beats, including with route speeds (D31)", "[geometry]") {
    const auto ticks = geometry::beatTicks(4 * kTicksPerQuarter);
    REQUIRE(ticks.size() == 3);
    CHECK(ticks[0].fraction == Catch::Approx(0.25));
    CHECK_FALSE(ticks[0].strong);
    CHECK(geometry::beatTicks(kTicksPerQuarter).empty());
    const auto eight = geometry::beatTicks(8 * kTicksPerQuarter);
    CHECK(eight[3].strong); // beat 4
    const auto fast = geometry::beatTicks(legDuration(3, GridSpacing{}, *RouteRate::make(3, 2))->value());
    REQUIRE(fast.size() == 1); // 3 cells at x3/2 = 2 beats
    CHECK(fast[0].fraction == Catch::Approx(0.5));
}

// --- Standard MIDI File (D28) -----------------------------------------------------------------

TEST_CASE("Captured events export as a type 1 Standard MIDI File", "[midi][capture]") {
    engine::EventLog log;
    engine::Sequencer seq{log};
    REQUIRE(seq.setDocument(*examples::make("polyrhythm")).ok());
    seq.play(5);
    seq.processUntil(MusicalTick{8 * kTicksPerQuarter});
    const auto bytes = midi::writeMidiFile(log.events(), {"Polyrhythm", 5, MilliBpm{}});
    REQUIRE(bytes.size() > 100);
    CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "MThd");
    CHECK(bytes[9] == 1);                    // type 1
    CHECK(bytes[11] == 3);                   // tempo track + channels 1 and 10
    CHECK(((bytes[12] << 8) | bytes[13]) == 960);
    const std::string all(bytes.begin(), bytes.end());
    CHECK(all.find("SignalFlux seed 5") != std::string::npos);
    CHECK(all.find(std::string("\xFF\x51\x03\x07\xA1\x20", 6)) != std::string::npos); // 500000 us per quarter
    std::size_t tracks = 0;
    for (std::size_t i = 0; i + 4 <= bytes.size(); ++i)
        if (bytes[i] == 'M' && bytes[i + 1] == 'T' && bytes[i + 2] == 'r' && bytes[i + 3] == 'k') ++tracks;
    CHECK(tracks == 3);
}

TEST_CASE("Exports are deterministic", "[midi][capture][determinism]") {
    auto run = [] {
        engine::EventLog log;
        engine::Sequencer seq{log};
        REQUIRE(seq.setDocument(*examples::make("wanderer")).ok());
        seq.play(11);
        seq.processUntil(MusicalTick{16 * kTicksPerQuarter});
        return midi::writeMidiFile(log.events(), {"Wanderer", 11, MilliBpm{}});
    };
    CHECK(run() == run());
}
