// SignalFlux — logic gates and Terminators (SF-006; D12, D13, P2, P3).
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

Gate makeGate(std::uint32_t id, GateType type, std::vector<GateInput> inputs, std::vector<std::uint32_t> targets) {
    Gate g;
    g.id = GateId{id};
    g.type = type;
    g.inputs = std::move(inputs);
    for (const auto t : targets) g.targets.push_back(StationId{t});
    return g;
}

/// 1 <-> 2 loop, one cell legs. Station 2 is gated by gate 1 = AND(switch 1).
test::ProjectBuilder gatedLoop(bool switchOn) {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62).route(1, 1, 2).route(2, 2, 1);
    b.doc().plots.front().switches.push_back(Switch{SwitchId{1}, "S", {5, 5}, switchOn});
    b.doc().plots.front().gates.push_back(makeGate(1, GateType::And, {SwitchInput{SwitchId{1}}}, {2}));
    return b;
}

std::vector<std::string> routingOnly(const EventLog& log) {
    std::vector<std::string> out;
    for (const auto& e : log.events())
        if (e.type == EngineEventType::TravelerArrive || e.type == EngineEventType::TravelerDepart ||
            e.type == EngineEventType::TravelerEnd || e.type == EngineEventType::TravelerSpawn)
            out.push_back(toCanonicalString(e));
    return out;
}

std::size_t noteOnsFrom(const EventLog& log, std::uint32_t station) {
    return static_cast<std::size_t>(std::count_if(log.events().begin(), log.events().end(), [&](const EngineEvent& e) {
        return e.type == EngineEventType::NoteOn && e.station == StationId{station};
    }));
}

} // namespace

// --- Logic gates (D12) ----------------------------------------------------------------

TEST_CASE("A false gate silences its Station but never changes routing (P2)", "[engine][logic]") {
    auto run = [](bool on) {
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(gatedLoop(on).doc()).ok());
        seq.play();
        seq.processUntil(MusicalTick{20 * Q});
        return log;
    };
    const auto open = run(true);
    const auto closed = run(false);
    CHECK(noteOnsFrom(open, 2) == 10);
    CHECK(noteOnsFrom(closed, 2) == 0);
    CHECK(noteOnsFrom(closed, 1) == noteOnsFrom(open, 1));
    CHECK(routingOnly(open) == routingOnly(closed));
}

TEST_CASE("Logic evaluates while stopped", "[engine][logic]") {
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(gatedLoop(false).doc()).ok());
    CHECK(seq.snapshot().gates.at(0).output == false);
    seq.setSwitch(SwitchId{1}, true);
    CHECK(seq.snapshot().gates.at(0).output == true);
    CHECK(seq.snapshot().stations.at(1).enabled);
    seq.setSwitch(SwitchId{1}, false);
    CHECK_FALSE(seq.snapshot().stations.at(1).enabled);
    CHECK(seq.transport() == TransportState::Stopped);
}

TEST_CASE("A Switch flipped mid-play takes effect from that tick", "[engine][logic]") {
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(gatedLoop(false).doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{4 * Q + 1});
    CHECK(noteOnsFrom(log, 2) == 0);
    seq.setSwitch(SwitchId{1}, true);
    seq.processUntil(MusicalTick{8 * Q + 1});
    CHECK(noteOnsFrom(log, 2) == 2); // arrivals at 5Q and 7Q
}

TEST_CASE("Station Active input follows the Station's sounding notes", "[engine][logic]") {
    // Station 1 (start) sounds at tick 0. Station 3 is reached at 960 from Start Station 2.
    // Station 3 is enabled only while Station 1 is sounding.
    auto build = [](std::int64_t duration1) {
        test::ProjectBuilder b;
        b.station(1, {0, 0}, 60, true).station(2, {0, 2}, 50, true).station(3, {1, 2}, 52).route(1, 2, 3);
        b.get(1).note.duration = MusicalTick{duration1};
        b.doc().plots.front().gates.push_back(makeGate(1, GateType::Or, {StationActiveInput{StationId{1}}}, {3}));
        return b.doc();
    };
    for (const auto& [duration, expect] : {std::pair{480, 0u}, std::pair{1920, 1u}}) {
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(build(duration)).ok());
        seq.play();
        seq.processUntil(MusicalTick{2 * Q});
        CHECK(noteOnsFrom(log, 3) == expect);
    }
}

TEST_CASE("Arrivals at tick T see gate state from before T (D21)", "[engine][logic]") {
    // Stations X (2) and Y (4) are both reached at 960. Y is enabled only while X sounds.
    // X starts sounding at 960 itself, so Y must still be silent, whichever Traveler is processed first.
    for (const bool yFirst : {false, true}) {
        test::ProjectBuilder b;
        const std::uint32_t sx = yFirst ? 3 : 1, sy = yFirst ? 1 : 3;
        b.station(sx, {0, 0}, 60, true).station(2, {1, 0}, 62);
        b.station(sy, {0, 2}, 60, true).station(4, {1, 2}, 64);
        b.route(10, sx, 2).route(11, sy, 4);
        b.doc().plots.front().gates.push_back(makeGate(1, GateType::Or, {StationActiveInput{StationId{2}}}, {4}));
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(b.doc()).ok());
        seq.play();
        seq.processUntil(MusicalTick{2 * Q});
        CHECK(noteOnsFrom(log, 2) == 1);
        CHECK(noteOnsFrom(log, 4) == 0);
    }
}

TEST_CASE("MIDI input drives gates, on a specific or any channel", "[engine][logic]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true);
    auto& gates = b.doc().plots.front().gates;
    gates.push_back(makeGate(1, GateType::Or, {MidiNoteInput{MidiChannel::of<2>(), MidiNote::of<36>()}}, {}));
    gates.push_back(makeGate(2, GateType::Or, {MidiCcInput{std::nullopt, CcNumber::of<74>(), CcValue::of<64>()}}, {}));
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());

    seq.setMidiNoteHeld(MidiChannel::of<1>(), MidiNote::of<36>(), true);
    CHECK_FALSE(seq.snapshot().gates[0].output); // wrong channel
    seq.setMidiNoteHeld(MidiChannel::of<2>(), MidiNote::of<36>(), true);
    CHECK(seq.snapshot().gates[0].output);
    seq.setMidiNoteHeld(MidiChannel::of<2>(), MidiNote::of<36>(), false);
    CHECK_FALSE(seq.snapshot().gates[0].output);

    seq.setMidiControl(MidiChannel::of<9>(), CcNumber::of<74>(), CcValue::of<63>());
    CHECK_FALSE(seq.snapshot().gates[1].output);
    seq.setMidiControl(MidiChannel::of<9>(), CcNumber::of<74>(), CcValue::of<64>());
    CHECK(seq.snapshot().gates[1].output);
}

TEST_CASE("Gate truth tables", "[engine][logic]") {
    struct Case { GateType type; bool a, b, expect; };
    const Case cases[] = {
        {GateType::And, true, true, true},   {GateType::And, true, false, false},
        {GateType::Or, false, false, false}, {GateType::Or, true, false, true},
        {GateType::Xor, true, true, false},  {GateType::Xor, true, false, true},
        {GateType::Nand, true, true, false}, {GateType::Nand, false, true, true},
        {GateType::Nor, false, false, true}, {GateType::Nor, true, false, false},
    };
    for (const auto& c : cases) {
        test::ProjectBuilder b;
        b.station(1, {0, 0});
        auto& plot = b.doc().plots.front();
        plot.switches = {Switch{SwitchId{1}, "a", {3, 3}, c.a}, Switch{SwitchId{2}, "b", {4, 4}, c.b}};
        plot.gates.push_back(makeGate(1, c.type, {SwitchInput{SwitchId{1}}, SwitchInput{SwitchId{2}}}, {}));
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(b.doc()).ok());
        CHECK(seq.snapshot().gates[0].output == c.expect);
    }
    test::ProjectBuilder b;
    b.station(1, {0, 0});
    b.doc().plots.front().switches = {Switch{SwitchId{1}, "a", {3, 3}, true}};
    b.doc().plots.front().gates.push_back(makeGate(1, GateType::Not, {SwitchInput{SwitchId{1}}}, {}));
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    CHECK_FALSE(seq.snapshot().gates[0].output);
}

TEST_CASE("A chain of gates settles within one step", "[engine][logic]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0});
    auto& plot = b.doc().plots.front();
    plot.switches = {Switch{SwitchId{1}, "a", {3, 3}, false}};
    // gate 3 = NOT(gate 2), gate 2 = NOT(gate 1), gate 1 = OR(switch). Ids are deliberately out of order.
    plot.gates.push_back(makeGate(3, GateType::Not, {GateOutputInput{GateId{2}}}, {}));
    plot.gates.push_back(makeGate(1, GateType::Or, {SwitchInput{SwitchId{1}}}, {}));
    plot.gates.push_back(makeGate(2, GateType::Not, {GateOutputInput{GateId{1}}}, {}));
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.setSwitch(SwitchId{1}, true);
    const auto snap = seq.snapshot();
    CHECK(snap.gates[0].output);       // gate 1
    CHECK_FALSE(snap.gates[1].output); // gate 2
    CHECK(snap.gates[2].output);       // gate 3
}

TEST_CASE("A NOT gate fed back into itself toggles once per evaluation step (D12)", "[engine][logic]") {
    test::ProjectBuilder b;
    b.station(1, {0, 0});
    auto& plot = b.doc().plots.front();
    plot.switches = {Switch{SwitchId{1}, "poke", {3, 3}, false}};
    plot.gates.push_back(makeGate(1, GateType::Not, {GateOutputInput{GateId{1}}}, {}));
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok()); // one step: !false = true
    std::vector<bool> seen{seq.snapshot().gates[0].output};
    for (int i = 0; i < 4; ++i) {
        seq.setSwitch(SwitchId{1}, i % 2 == 0); // each external input runs one step
        seen.push_back(seq.snapshot().gates[0].output);
    }
    CHECK(seen == std::vector<bool>{true, false, true, false, true});
}

TEST_CASE("Cyclic logic during playback is deterministic", "[engine][logic][determinism]") {
    auto doc = *examples::make("gatekeeper");
    auto run = [&] {
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(doc).ok());
        seq.play();
        seq.processUntil(MusicalTick{64 * Q});
        return log.canonical();
    };
    CHECK(run() == run());
}

TEST_CASE("Gates never redirect or delete Travelers, whatever they do", "[engine][logic]") {
    auto withGates = *examples::make("gatekeeper");
    auto withoutGates = withGates;
    withoutGates.plots.front().gates.clear();
    for (auto& s : withoutGates.plots.front().stations) s.terminatorLimit.reset();
    for (auto& s : withGates.plots.front().stations) s.terminatorLimit.reset();
    auto run = [](const ProjectDocument& doc) {
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(doc).ok());
        seq.play();
        seq.processUntil(MusicalTick{64 * Q});
        return routingOnly(log);
    };
    CHECK(run(withGates) == run(withoutGates));
}

// --- Terminators (D13) ------------------------------------------------------------------

namespace {
test::ProjectBuilder terminatorLoop(std::uint32_t limit) {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 60, true).station(2, {1, 0}, 62).route(1, 1, 2).route(2, 2, 1);
    b.get(2).terminatorLimit = limit;
    return b;
}
} // namespace

TEST_CASE("Terminator lets L arrivals through, then ends the chain", "[engine][terminator]") {
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(terminatorLoop(2).doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{20 * Q});
    CHECK(noteOnsFrom(log, 2) == 2);
    CHECK(contains(log, "4800 End t=1 st=2 why=terminated")); // third arrival: 960, 2880, 4800
    CHECK(seq.snapshot().travelers.empty());
    CHECK(seq.snapshot().stations[1].terminatorCount == 3);
}

TEST_CASE("Limit 0 is Block: every arrival ends, nothing plays", "[engine][terminator]") {
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(terminatorLoop(0).doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{4 * Q});
    CHECK(noteOnsFrom(log, 2) == 0);
    CHECK(contains(log, "960 End t=1 st=2 why=terminated"));
}

TEST_CASE("Silent arrivals still count toward the Terminator", "[engine][terminator]") {
    auto b = terminatorLoop(2);
    b.get(2).manuallyMuted = true;
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{20 * Q});
    CHECK(contains(log, "4800 End t=1 st=2 why=terminated"));
}

TEST_CASE("Terminator counts reset on Play, survive Continue and edits", "[engine][terminator]") {
    auto b = terminatorLoop(5);
    EventLog log;
    Sequencer seq{log};
    REQUIRE(seq.setDocument(b.doc()).ok());
    seq.play();
    seq.processUntil(MusicalTick{3 * Q + 1}); // arrivals at 960 and 2880
    CHECK(seq.snapshot().stations[1].terminatorCount == 2);
    seq.requestStop();
    seq.continuePlayback();
    REQUIRE(seq.setDocument(b.doc()).ok()); // an edit
    CHECK(seq.snapshot().stations[1].terminatorCount == 2);
    seq.play();
    CHECK(seq.snapshot().stations[1].terminatorCount == 0);
}

TEST_CASE("Terminator counts are runtime-only: a reloaded document starts from zero", "[engine][terminator]") {
    const auto doc = terminatorLoop(1).doc();
    auto run = [&] {
        EventLog log;
        Sequencer seq{log};
        REQUIRE(seq.setDocument(doc).ok());
        seq.play();
        seq.processUntil(MusicalTick{10 * Q});
        return log.canonical();
    };
    CHECK(run() == run()); // nothing persisted in the document changes the outcome
}
