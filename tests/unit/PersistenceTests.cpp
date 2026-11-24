// SignalFlux — project file tests (SF-010; D17, D25; ACCEPTANCE §5).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

#include "domain/Examples.h"
#include "domain/Validation.h"
#include "engine/EventLog.h"
#include "persistence/ProjectSerializer.h"
#include "TestProjects.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace signalflux;
using namespace signalflux::persistence;
namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// A document that sets every field to a non-default value.
ProjectDocument everything() {
    test::ProjectBuilder b;
    b.station(1, {0, 0}, 61, true).station(2, {3, 2}, 70).station(3, {5, 5}, 40);
    b.route(10, 1, 2).route(11, 2, 3).route(12, 3, 1).route(13, 1, 3);
    auto& doc = b.doc();
    doc.metadata = {"Everything", "Omnico Audio", "Notes with \"quotes\", unicode: éè, and\nnewlines"};

    auto& s1 = b.get(1);
    s1.name = "Start";
    s1.colour = 5;
    s1.note.mode = NoteMode::RandomValue;
    s1.note.random = {RandomValueMode::Walk, MidiNote::of<69>(), 3, 9};
    s1.note.chordOffsets = {0, 2, 4};
    s1.note.velocity = Velocity::of<90>();
    s1.note.duration = MusicalTick{720};
    s1.channel = MidiChannel::of<16>();
    s1.probability = ProbabilityPercent::of<0>();
    s1.additionalRepeats = 3;
    s1.repeatInterval = MusicalTick{160};
    s1.branchMode = BranchMode::Instant;
    s1.terminatorLimit = 0;
    s1.manuallyMuted = true;
    s1.humanize = {12, MusicalTick{30}};
    s1.outgoingOrder = {FlowpointId{13}, FlowpointId{10}};

    auto& s2 = b.get(2);
    s2.eventType = StationEventType::ControlChange;
    s2.controlChange = {CcNumber::of<74>(), CcValue::of<100>(), true};
    s2.note.mode = NoteMode::Relative;
    s2.note.relativeSteps = -7;
    s2.note.relativePassThrough = true;
    s2.note.pass = true;

    auto& s3 = b.get(3);
    s3.eventType = StationEventType::PitchBend;
    s3.pitchBend = {PitchBendValue::of<-8192>(), true};
    s3.note.forceScale = true;
    s3.branchMode = BranchMode::Random;

    auto& plot = doc.plots.front();
    plot.name = "Main";
    plot.width = 32;
    plot.height = 40;
    plot.flowpoints[0].elbow = RouteElbow::VerticalFirst;
    plot.flowpoints[0].weight = RouteWeight::of<0>();
    plot.flowpoints[1].rate = *RouteRate::make(3, 2);
    plot.switches = {Switch{SwitchId{1}, "A", {9, 9}, true}};
    Gate g;
    g.id = GateId{1};
    g.name = "gate";
    g.position = {7, 7};
    g.type = GateType::Xor;
    g.inputs = {StationActiveInput{StationId{1}}, GateOutputInput{GateId{1}}, SwitchInput{SwitchId{1}},
                MidiNoteInput{std::nullopt, MidiNote::of<36>()}, MidiCcInput{MidiChannel::of<3>(), CcNumber::of<1>(), CcValue::of<64>()}};
    g.targets = {StationId{3}};
    plot.gates = {g};

    doc.scenes = {Scene{SceneId{1}, "Verse", {StationId{2}}, {SwitchState{SwitchId{1}, false}}, MilliBpm::of<96'500>()},
                  Scene{SceneId{2}, "Chorus", {}, {}, std::nullopt}};

    auto& st = doc.settings;
    st.tempo = MilliBpm::of<133'333>();
    st.gridSpacing = *GridSpacing::make(480); // 3/2 rate needs spacing x 2 divisible by 3
    st.root = PitchClass::of<10>();
    st.scale = maskFor(ScalePreset::Dorian);
    st.randomSeed = 0xFFFF'FFFF'FFFF'FFFFull; // beyond what JavaScript numbers can hold
    st.seedMode = SeedMode::Fixed;
    st.starredSeeds = {1, 9'007'199'254'740'993ull};
    st.swingPercent = 66;
    st.swingGrid = SwingGrid::Eighths;
    st.travelerCap = 64;
    st.activeNoteCap = 512;
    st.sceneQuantize = SceneQuantize::NextFourBeats;
    st.linkEnabled = true;
    st.captureMinutes = 30;

    auto& midi = doc.midi;
    midi.outputs = {{"IAC Driver Bus 1", "com.apple.iac.1"}};
    midi.inputs = {{"Keystep", "ks-1"}, {"Launch Control", ""}};
    midi.virtualOutputPort = false;
    midi.sendClock = false;
    midi.clockWhileStopped = true;
    midi.songPositionBeforeStart = false;
    MidiMapping m1;
    m1.id = MappingId{1};
    m1.source = {std::string("Launch Control"), MidiChannel::of<1>(), MidiSourceType::ControlChange, 21};
    m1.target = MappingTarget::StationProbability;
    m1.station = StationId{2};
    MidiMapping m2;
    m2.id = MappingId{2};
    m2.source = {std::nullopt, std::nullopt, MidiSourceType::Note, 48};
    m2.target = MappingTarget::SceneRecall;
    m2.scene = SceneId{2};
    m2.rangeSteps = 7;
    midi.mappings = {m1, m2};
    return doc;
}

} // namespace

TEST_CASE("Round trip preserves every field", "[persistence]") {
    const auto doc = everything();
    const auto report = validateProject(doc);
    for (const auto& i : report.issues) UNSCOPED_INFO(i.path << ": " << i.message);
    REQUIRE(report.ok());
    const auto loaded = fromJson(toJson(doc));
    INFO((loaded.errors.empty() ? std::string{} : loaded.errors.front()));
    REQUIRE(loaded.ok());
    CHECK(*loaded.document == doc);
}

TEST_CASE("Writing is canonical: same document, same bytes, and load-save is stable", "[persistence][determinism]") {
    const auto doc = everything();
    const auto a = toJson(doc);
    CHECK(a == toJson(doc));
    CHECK(toJson(*fromJson(a).document) == a);
    CHECK(a.back() == '\n');
    CHECK(a.find('\r') == std::string::npos);
    CHECK(a.find("\"randomSeed\": \"18446744073709551615\"") != std::string::npos); // seeds as text
}

TEST_CASE("Example project files match the built-in examples byte for byte", "[persistence][determinism]") {
    for (const auto name : examples::names()) {
        INFO("example: " << name);
        const fs::path file = fs::path(SIGNALFLUX_EXAMPLES_DIR) / (std::string(name) + kFileExtension);
        const auto onDisk = readFile(file);
        REQUIRE_FALSE(onDisk.empty());
        CHECK(toJson(*examples::make(name)) == onDisk);
        const auto loaded = loadFromFile(file);
        REQUIRE(loaded.ok());
        CHECK(*loaded.document == *examples::make(name));
    }
}

TEST_CASE("A project loaded from file plays identically to the in-memory one", "[persistence][determinism]") {
    const auto doc = *examples::make("gatekeeper");
    const auto loaded = fromJson(toJson(doc));
    REQUIRE(loaded.ok());
    const MusicalTick len{32 * kTicksPerQuarter};
    CHECK(engine::renderCanonicalLog(*loaded.document, len) == engine::renderCanonicalLog(doc, len));
}

TEST_CASE("Unknown future schema versions fail gracefully", "[persistence]") {
    auto text = toJson(makeDefaultProject());
    text.replace(text.find("\"schemaVersion\": 1"), 18, "\"schemaVersion\": 7");
    const auto r = fromJson(text);
    CHECK_FALSE(r.ok());
    REQUIRE(r.errors.size() == 1);
    CHECK(r.errors[0].find("newer SignalFlux") != std::string::npos);
    CHECK(r.fileSchemaVersion == 7);
}

TEST_CASE("Older schemas are upgraded through the migration chain", "[persistence]") {
    // Simulated legacy format 0: tempo stored as whole BPM at the top level.
    const std::string legacy = R"({"format": "signalflux-project", "schemaVersion": 0, "bpm": 90,
                                   "plots": [{"id": 1, "name": "Old", "width": 8, "height": 8}]})";
    MigrationRegistry registry;
    registry.add(0, [](const std::string& json) {
        // A real migration would use a JSON library; this fixture only needs a textual rewrite.
        std::string out = json;
        out.replace(out.find("\"schemaVersion\": 0"), 18, "\"schemaVersion\": 1");
        out.replace(out.find("\"bpm\": 90"), 9, "\"settings\": {\"tempoMilliBpm\": 90000}");
        return out;
    });
    const auto r = fromJson(legacy, registry);
    INFO((r.errors.empty() ? std::string{} : r.errors.front()));
    REQUIRE(r.ok());
    CHECK(r.document->settings.tempo.value() == 90'000);
    CHECK(r.document->plots.front().name == "Old");
    CHECK(r.fileSchemaVersion == 0);
    REQUIRE_FALSE(r.warnings.empty());
    CHECK(r.warnings.front().find("Upgraded") != std::string::npos);

    CHECK_FALSE(fromJson(legacy).ok()); // the built-in registry has no format-0 migration
}

TEST_CASE("Corrupt or foreign files report a useful error and no document", "[persistence]") {
    CHECK(fromJson("{ not json").errors.front().find("isn't valid JSON") != std::string::npos);
    CHECK(fromJson("[1, 2]").errors.front().find("isn't valid JSON") != std::string::npos);
    CHECK(fromJson(R"({"format": "something-else", "schemaVersion": 1})").errors.front().find("format") != std::string::npos);
    CHECK_FALSE(fromJson(R"({"format": "signalflux-project"})").ok());

    auto text = toJson(*examples::make("first-loop"));
    text.replace(text.find("\"channel\": 1"), 12, "\"channel\": 17");
    const auto r = fromJson(text);
    CHECK_FALSE(r.ok());
    CHECK_FALSE(r.document.has_value());
    CHECK(r.errors.front().find("channel") != std::string::npos);
}

TEST_CASE("A structurally invalid document is refused, not partially loaded", "[persistence]") {
    auto doc = *examples::make("first-loop");
    doc.plots.front().stations[1].position = doc.plots.front().stations[0].position; // two Stations, one cell
    const auto r = fromJson(toJson(doc));
    CHECK_FALSE(r.ok());
    CHECK_FALSE(r.document.has_value());
}

TEST_CASE("Missing MIDI devices never block loading", "[persistence]") {
    auto doc = *examples::make("first-loop");
    doc.midi.outputs = {{"A synth that is not plugged in", "nope"}};
    doc.midi.inputs = {{"Gone", ""}};
    const auto r = fromJson(toJson(doc));
    REQUIRE(r.ok());
    CHECK(r.document->midi.outputs.front().name == "A synth that is not plugged in");
}

TEST_CASE("Unknown keys are ignored so newer minor additions don't break older readers", "[persistence]") {
    auto text = toJson(*examples::make("first-loop"));
    text.insert(text.find("\"format\""), "\"someFutureThing\": {\"x\": 1},\n  ");
    CHECK(fromJson(text).ok());
}

TEST_CASE("Atomic save replaces the file and leaves no temporary files", "[persistence]") {
    const auto dir = fs::temp_directory_path() / "signalflux-persistence-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto path = dir / "song.sflux";

    REQUIRE_FALSE(saveToFile(*examples::make("first-loop"), path));
    REQUIRE_FALSE(saveToFile(*examples::make("wanderer"), path));
    CHECK(loadFromFile(path).document->metadata.name == "Wanderer");
    std::size_t files = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator(dir)) ++files;
    CHECK(files == 1);
    fs::remove_all(dir);
}

TEST_CASE("Saving into a missing folder fails without touching anything", "[persistence]") {
    const auto path = fs::temp_directory_path() / "signalflux-no-such-dir" / "deeper" / "song.sflux";
    const auto err = saveToFile(makeDefaultProject(), path);
    REQUIRE(err);
    CHECK_FALSE(fs::exists(path));
}

TEST_CASE("Loading never starts playback: a document carries no transport state", "[persistence]") {
    // Structural guarantee: ProjectDocument has no transport field, and loading
    // returns data only. The app decides when to Play (SF-010 acceptance).
    const auto r = fromJson(toJson(*examples::make("first-loop")));
    REQUIRE(r.ok());
    CHECK(r.document->settings.seedMode == SeedMode::NewEachPlay);
}
