// SignalFlux — project validation tests (SF-002: "Graph references are validated").
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Validation.h"

#include <catch2/catch_test_macros.hpp>

using namespace signalflux;

namespace {

Station makeStation(std::uint32_t id, GridCoordinate pos, bool start = false) {
    Station s;
    s.id = StationId{id};
    s.name = "S" + std::to_string(id);
    s.position = pos;
    s.isStart = start;
    return s;
}

Flowpoint makeFlowpoint(std::uint32_t id, std::uint32_t from, std::uint32_t to) {
    Flowpoint f;
    f.id = FlowpointId{id};
    f.from = StationId{from};
    f.to = StationId{to};
    return f;
}

/// A valid three-Station loop: 1 -> 2 -> 3 -> 1, Station 1 is the Start.
ProjectDocument makeLoop() {
    auto doc = makeDefaultProject();
    auto& plot = doc.plots.front();
    plot.stations = {makeStation(1, {0, 0}, true), makeStation(2, {3, 2}), makeStation(3, {5, 0})};
    plot.flowpoints = {makeFlowpoint(10, 1, 2), makeFlowpoint(11, 2, 3), makeFlowpoint(12, 3, 1)};
    plot.stations[0].outgoingOrder = {FlowpointId{10}};
    plot.stations[1].outgoingOrder = {FlowpointId{11}};
    plot.stations[2].outgoingOrder = {FlowpointId{12}};
    return doc;
}

Plot& plotOf(ProjectDocument& doc) { return doc.plots.front(); }

} // namespace

TEST_CASE("Default and loop projects are valid", "[domain][validation]") {
    CHECK(validateProject(makeDefaultProject()).ok());
    CHECK(validateProject(makeDefaultProject()).issues.empty());
    const auto report = validateProject(makeLoop());
    CHECK(report.ok());
    CHECK(report.issues.empty());
}

TEST_CASE("Document structure", "[domain][validation]") {
    SECTION("exactly one Plot (D1)") {
        auto doc = makeLoop();
        Plot second;
        second.id = PlotId{2};
        doc.plots.push_back(second);
        CHECK(validateProject(doc).has(IssueCode::PlotCountNotOne));
        doc.plots.clear();
        CHECK(validateProject(doc).has(IssueCode::PlotCountNotOne));
    }
    SECTION("schema version must be current in memory") {
        auto doc = makeLoop();
        doc.schemaVersion.value = 99;
        CHECK(validateProject(doc).has(IssueCode::UnsupportedSchemaVersion));
    }
    SECTION("at most one MIDI output in v1 (D16)") {
        auto doc = makeLoop();
        doc.midi.outputs = {{"A", "a"}, {"B", "b"}};
        CHECK(validateProject(doc).has(IssueCode::TooManyOutputs));
    }
    SECTION("at most 8 scenes (D35)") {
        auto doc = makeLoop();
        for (std::uint32_t i = 1; i <= 9; ++i) doc.scenes.push_back(Scene{SceneId{i}, "x", {}, {}, std::nullopt});
        CHECK(validateProject(doc).has(IssueCode::TooManyScenes));
    }
}

TEST_CASE("Identity", "[domain][validation]") {
    auto doc = makeLoop();
    SECTION("ID 0 is invalid") {
        plotOf(doc).stations[1].id = StationId{};
        CHECK(validateProject(doc).has(IssueCode::InvalidId));
    }
    SECTION("duplicate Station IDs") {
        plotOf(doc).stations[1].id = StationId{1};
        CHECK(validateProject(doc).has(IssueCode::DuplicateId));
    }
    SECTION("duplicate Flowpoint IDs") {
        plotOf(doc).flowpoints[1].id = FlowpointId{10};
        CHECK(validateProject(doc).has(IssueCode::DuplicateId));
    }
}

TEST_CASE("Grid placement (D1)", "[domain][validation]") {
    auto doc = makeLoop();
    SECTION("one Station per cell") {
        plotOf(doc).stations[1].position = {0, 0};
        CHECK(validateProject(doc).has(IssueCode::StationCellOccupied));
    }
    SECTION("Stations inside the Plot") {
        plotOf(doc).stations[1].position = {64, 0};
        CHECK(validateProject(doc).has(IssueCode::StationOutOfBounds));
        plotOf(doc).stations[1].position = {0, -1};
        CHECK(validateProject(doc).has(IssueCode::StationOutOfBounds));
    }
    SECTION("Plot size limits") {
        plotOf(doc).width = 0;
        CHECK(validateProject(doc).has(IssueCode::PlotSizeOutOfRange));
    }
}

TEST_CASE("Flowpoint references", "[domain][validation]") {
    auto doc = makeLoop();
    SECTION("unknown endpoint") {
        plotOf(doc).flowpoints[0].to = StationId{99};
        CHECK(validateProject(doc).has(IssueCode::FlowpointUnknownStation));
    }
    SECTION("self-loop") {
        plotOf(doc).flowpoints[0].to = StationId{1};
        CHECK(validateProject(doc).has(IssueCode::FlowpointSelfLoop));
    }
    SECTION("waypoints reserved") {
        plotOf(doc).flowpoints[0].waypoints = {{1, 1}};
        CHECK(validateProject(doc).has(IssueCode::FlowpointWaypointsUnsupported));
    }
    SECTION("inexact route rate at current grid spacing (D33)") {
        doc.settings.gridSpacing = *GridSpacing::make(80);
        plotOf(doc).flowpoints[0].rate = *RouteRate::make(3, 1);
        CHECK(validateProject(doc).has(IssueCode::RouteRateInexact));
    }
}

TEST_CASE("Outgoing order lists exactly the Station's outgoing Flowpoints (D6)", "[domain][validation]") {
    auto doc = makeLoop();
    SECTION("missing entry") {
        plotOf(doc).stations[0].outgoingOrder.clear();
        CHECK(validateProject(doc).has(IssueCode::OutgoingOrderMismatch));
    }
    SECTION("duplicate entry") {
        plotOf(doc).stations[0].outgoingOrder = {FlowpointId{10}, FlowpointId{10}};
        CHECK(validateProject(doc).has(IssueCode::OutgoingOrderMismatch));
    }
    SECTION("someone else's Flowpoint") {
        plotOf(doc).stations[0].outgoingOrder = {FlowpointId{10}, FlowpointId{11}};
        CHECK(validateProject(doc).has(IssueCode::OutgoingOrderMismatch));
    }
    SECTION("reordering a valid list is fine") {
        plotOf(doc).flowpoints.push_back(makeFlowpoint(13, 1, 3));
        plotOf(doc).stations[0].outgoingOrder = {FlowpointId{13}, FlowpointId{10}};
        CHECK(validateProject(doc).ok());
    }
}

TEST_CASE("Weighted Random with all weights zero warns (D32)", "[domain][validation]") {
    auto doc = makeLoop();
    plotOf(doc).stations[0].branchMode = BranchMode::Random;
    plotOf(doc).flowpoints[0].weight = *RouteWeight::make(0);
    const auto report = validateProject(doc);
    CHECK(report.ok());
    CHECK(report.has(IssueCode::AllRandomWeightsZero));
}

TEST_CASE("Station parameter ranges", "[domain][validation]") {
    auto doc = makeLoop();
    auto& st = plotOf(doc).stations[0];
    SECTION("duration") {
        st.note.duration = MusicalTick{0};
        CHECK(validateProject(doc).has(IssueCode::ValueOutOfRange));
    }
    SECTION("repeats") {
        st.additionalRepeats = 65;
        CHECK(validateProject(doc).has(IssueCode::ValueOutOfRange));
    }
    SECTION("repeat interval") {
        st.repeatInterval = MusicalTick{0};
        CHECK(validateProject(doc).has(IssueCode::ValueOutOfRange));
    }
    SECTION("relative steps") {
        st.note.relativeSteps = -49;
        CHECK(validateProject(doc).has(IssueCode::ValueOutOfRange));
    }
    SECTION("humanize") {
        st.humanize.maxDelay = MusicalTick{481};
        CHECK(validateProject(doc).has(IssueCode::ValueOutOfRange));
    }
    SECTION("project caps") {
        doc.settings.travelerCap = 0;
        CHECK(validateProject(doc).has(IssueCode::ValueOutOfRange));
        doc.settings.travelerCap = 257;
        CHECK(validateProject(doc).has(IssueCode::ValueOutOfRange));
    }
    SECTION("swing") {
        doc.settings.swingPercent = 76;
        CHECK(validateProject(doc).has(IssueCode::ValueOutOfRange));
    }
}

TEST_CASE("Default Traveler cap is 8 (owner decision, D23)", "[domain][validation]") {
    CHECK(makeDefaultProject().settings.travelerCap == 8);
}

TEST_CASE("Chord offsets (D34)", "[domain][validation]") {
    auto doc = makeLoop();
    auto& note = plotOf(doc).stations[0].note;
    note.chordOffsets = {0, 2, 4};
    CHECK(validateProject(doc).ok());
    note.chordOffsets = {2, 4};            // no root
    CHECK(validateProject(doc).has(IssueCode::ChordInvalid));
    note.chordOffsets = {0, 2, 2};         // duplicate
    CHECK(validateProject(doc).has(IssueCode::ChordInvalid));
    note.chordOffsets = {0, 1, 2, 3, 4, 5, 6}; // too many
    CHECK(validateProject(doc).has(IssueCode::ChordInvalid));
    note.chordOffsets = {0, 25};           // out of range
    CHECK(validateProject(doc).has(IssueCode::ChordInvalid));
}

TEST_CASE("Mode-specific flags warn when they have no effect", "[domain][validation]") {
    auto doc = makeLoop();
    auto& note = plotOf(doc).stations[0].note;
    note.forceScale = true;
    note.mode = NoteMode::Relative;
    auto report = validateProject(doc);
    CHECK(report.ok());
    CHECK(report.has(IssueCode::ForceScaleIgnoredOutsideAbsolute));

    note.forceScale = false;
    note.mode = NoteMode::Absolute;
    note.relativePassThrough = true;
    report = validateProject(doc);
    CHECK(report.has(IssueCode::PassThroughIgnoredOutsideRelative));
}

TEST_CASE("No Start Station warns but is not an error", "[domain][validation]") {
    auto doc = makeLoop();
    plotOf(doc).stations[0].isStart = false;
    const auto report = validateProject(doc);
    CHECK(report.ok());
    CHECK(report.has(IssueCode::NoStartStation));
}

TEST_CASE("Logic gates (D12)", "[domain][validation]") {
    auto doc = makeLoop();
    auto& plot = plotOf(doc);
    plot.switches.push_back(Switch{SwitchId{1}, "A", {10, 10}, false});

    Gate g;
    g.id = GateId{1};
    g.type = GateType::And;
    g.inputs = {SwitchInput{SwitchId{1}}, StationActiveInput{StationId{2}}};
    g.targets = {StationId{3}};
    plot.gates.push_back(g);
    REQUIRE(validateProject(doc).ok());

    SECTION("NOT takes exactly one input") {
        plot.gates[0].type = GateType::Not;
        CHECK(validateProject(doc).has(IssueCode::GateArity));
    }
    SECTION("a gate with no inputs is a warning, not an error") {
        plot.gates[0].inputs.clear();
        const auto r = validateProject(doc);
        CHECK(r.ok());
        CHECK(r.has(IssueCode::GateUnconnected));
    }
    SECTION("unknown references") {
        plot.gates[0].inputs.push_back(SwitchInput{SwitchId{9}});
        CHECK(validateProject(doc).has(IssueCode::GateUnknownReference));
    }
    SECTION("unknown target") {
        plot.gates[0].targets.push_back(StationId{99});
        CHECK(validateProject(doc).has(IssueCode::GateUnknownReference));
    }
    SECTION("cycles, including self-feedback, are allowed") {
        plot.gates[0].inputs.push_back(GateOutputInput{GateId{1}});
        CHECK(validateProject(doc).ok());
    }
    SECTION("a Station may have only one driving gate") {
        Gate g2 = plot.gates[0];
        g2.id = GateId{2};
        plot.gates.push_back(g2);
        CHECK(validateProject(doc).has(IssueCode::StationMultipleDrivers));
    }
    SECTION("listing the same target twice in one gate is not two drivers") {
        plot.gates[0].targets.push_back(StationId{3});
        CHECK_FALSE(validateProject(doc).has(IssueCode::StationMultipleDrivers));
    }
}

TEST_CASE("Scenes and mappings reference real objects", "[domain][validation]") {
    auto doc = makeLoop();
    SECTION("scene muting an unknown Station") {
        doc.scenes.push_back(Scene{SceneId{1}, "A", {StationId{42}}, {}, std::nullopt});
        CHECK(validateProject(doc).has(IssueCode::SceneUnknownReference));
    }
    SECTION("Station-targeted mapping without a Station") {
        MidiMapping m;
        m.id = MappingId{1};
        m.target = MappingTarget::StationVelocity;
        doc.midi.mappings.push_back(m);
        CHECK(validateProject(doc).has(IssueCode::MappingMissingTarget));
    }
    SECTION("scene recall mapping to an unknown scene") {
        MidiMapping m;
        m.id = MappingId{1};
        m.target = MappingTarget::SceneRecall;
        m.scene = SceneId{5};
        doc.midi.mappings.push_back(m);
        CHECK(validateProject(doc).has(IssueCode::MappingUnknownReference));
    }
}

TEST_CASE("Reports are deterministic: same input, same issues in the same order", "[domain][validation]") {
    auto doc = makeLoop();
    plotOf(doc).stations[1].position = {0, 0};
    plotOf(doc).flowpoints[0].to = StationId{99};
    plotOf(doc).stations[2].note.duration = MusicalTick{0};
    const auto a = validateProject(doc);
    const auto b = validateProject(doc);
    REQUIRE(a.issues.size() == b.issues.size());
    REQUIRE(a.issues.size() >= 3);
    for (std::size_t i = 0; i < a.issues.size(); ++i) {
        CHECK(a.issues[i].code == b.issues[i].code);
        CHECK(a.issues[i].path == b.issues[i].path);
    }
    CHECK(a.issues.front().path == "plots[0].stations[1].position");
}
