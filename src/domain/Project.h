// SignalFlux — project document model (task SF-002).
// Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.
//
// Plain serializable data. No UI, JUCE, device, or runtime state lives here:
// runtime counters (round-robin index, terminator count, Traveler positions)
// belong to the engine and are deliberately absent (D6, D13).
//
// Thread ownership: a ProjectDocument is owned and mutated by the
// engine/control thread. Other threads see immutable copies or snapshots.

#pragma once

#include "domain/Scale.h"
#include "domain/Types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace signalflux {

// ---------------------------------------------------------------------------
// Stations
// ---------------------------------------------------------------------------

enum class StationEventType : std::uint8_t { Note, ControlChange, PitchBend }; // D26
enum class NoteMode : std::uint8_t { Absolute, Relative, RandomValue };
enum class RandomValueMode : std::uint8_t { Anchor, Walk };                     // D10
enum class BranchMode : std::uint8_t { Sequential, Random, Instant };

struct RandomValueSettings {
    RandomValueMode mode = RandomValueMode::Anchor;
    MidiNote anchor;
    std::int32_t rangeSteps = 2;  ///< R: draw k in [-R, +R] scale steps. 0–24.
    std::int32_t boundSteps = 7;  ///< W (Walk only): stay within ±W steps of the anchor. 0–48.
};

struct NoteSettings {
    NoteMode mode = NoteMode::Absolute;
    MidiNote absoluteNote;                 ///< Absolute mode.
    std::int32_t relativeSteps = 0;        ///< Relative mode: signed scale steps, −48…48.
    bool forceScale = false;               ///< Absolute mode only (D11).
    bool pass = false;                     ///< Borrow the carried note; overrides Relative Values.
    bool relativePassThrough = false;      ///< Relative mode only.
    RandomValueSettings random;
    /// Chord (D34): scale-step offsets from the resolved root. Empty = single note.
    /// Otherwise 1–6 unique offsets in −24…24 that include 0 (the root).
    std::vector<std::int32_t> chordOffsets;
    Velocity velocity;
    MusicalTick duration{480};             ///< D18/D20: fixed duration, 1…16 quarters.
};

struct ControlChangeSettings {
    CcNumber controller;
    CcValue target;
    bool ramp = false;                     ///< Glide to target over the Station's duration.
};

struct PitchBendSettings {
    PitchBendValue target;
    bool ramp = false;
};

struct HumanizeSettings {                  // D36
    std::int32_t velocityRange = 0;        ///< ± velocity, 0–64.
    MusicalTick maxDelay{0};               ///< Delay-only timing jitter, 0–480 ticks.
};

struct Station {
    StationId id;
    std::string name;
    GridCoordinate position;
    bool isStart = false;

    StationEventType eventType = StationEventType::Note;
    NoteSettings note;
    ControlChangeSettings controlChange;
    PitchBendSettings pitchBend;

    MidiChannel channel;
    ProbabilityPercent probability;

    std::uint32_t additionalRepeats = 0;           ///< D8: extra triggers after the first, 0–64.
    std::optional<MusicalTick> repeatInterval;     ///< D8: nullopt = note duration.

    BranchMode branchMode = BranchMode::Sequential;
    /// D6: explicit, user-ordered list of this Station's outgoing Flowpoints.
    /// Must contain exactly the Flowpoints whose `from` is this Station.
    std::vector<FlowpointId> outgoingOrder;

    std::optional<std::uint32_t> terminatorLimit;  ///< D13: nullopt = no terminator; 0 = Block.
    bool manuallyMuted = false;
    HumanizeSettings humanize;
};

// ---------------------------------------------------------------------------
// Flowpoints (directed connections drawn as orthogonal routes)
// ---------------------------------------------------------------------------

enum class RouteElbow : std::uint8_t { HorizontalFirst, VerticalFirst };

struct Flowpoint {
    FlowpointId id;
    StationId from;
    StationId to;
    RouteElbow elbow = RouteElbow::HorizontalFirst;
    /// Reserved for orthogonal waypoints in a later schema (D2). Must be empty in v1.
    std::vector<GridCoordinate> waypoints;
    RouteWeight weight;                            ///< D32
    RouteRate rate;                                ///< D33
};

// ---------------------------------------------------------------------------
// Logic (D12)
// ---------------------------------------------------------------------------

enum class GateType : std::uint8_t { And, Or, Xor, Not, Nand, Nor };

struct StationActiveInput { StationId station; };
struct GateOutputInput    { GateId gate; };
struct SwitchInput        { SwitchId sw; };
struct MidiNoteInput      { std::optional<MidiChannel> channel; MidiNote note; };
struct MidiCcInput        { std::optional<MidiChannel> channel; CcNumber controller; CcValue threshold; };

using GateInput = std::variant<StationActiveInput, GateOutputInput, SwitchInput, MidiNoteInput, MidiCcInput>;

struct Gate {
    GateId id;
    std::string name;
    GridCoordinate position;
    GateType type = GateType::And;
    std::vector<GateInput> inputs;                 ///< NOT: exactly 1. Others: 1–16.
    std::vector<StationId> targets;                ///< Stations whose enable this gate drives.
};

struct Switch {
    SwitchId id;
    std::string name;
    GridCoordinate position;
    bool on = false;
};

// ---------------------------------------------------------------------------
// Plot (D1): the canvas. v1 projects contain exactly one.
// ---------------------------------------------------------------------------

struct Plot {
    PlotId id{1};
    std::string name = "Plot 1";
    std::int32_t width = 64;                       ///< Cells, 1–1024.
    std::int32_t height = 64;
    std::vector<Station> stations;
    std::vector<Flowpoint> flowpoints;
    std::vector<Gate> gates;
    std::vector<Switch> switches;
};

// ---------------------------------------------------------------------------
// Scenes (D35)
// ---------------------------------------------------------------------------

struct SwitchState {
    SwitchId sw;
    bool on = false;
};

struct Scene {
    SceneId id;
    std::string name;
    std::vector<StationId> mutedStations;          ///< Manual mutes; all others unmuted on recall.
    std::vector<SwitchState> switchStates;
    std::optional<MilliBpm> tempo;
};

// ---------------------------------------------------------------------------
// MIDI settings (D15, D16, D30)
// ---------------------------------------------------------------------------

struct MidiDeviceRef {
    std::string name;
    std::string identifier;                        ///< Platform identifier; may not exist on another machine.
};

enum class MidiSourceType : std::uint8_t { Note, ControlChange, PitchBend };

struct MidiSource {
    std::optional<std::string> deviceName;         ///< nullopt = any input device.
    std::optional<MidiChannel> channel;            ///< nullopt = any channel.
    MidiSourceType type = MidiSourceType::ControlChange;
    std::int32_t number = 0;                       ///< Note or CC number, 0–127. Ignored for Pitch Bend.
};

enum class MappingTarget : std::uint8_t {
    GlobalTranspose,     ///< Scale steps, ±rangeSteps.
    Tempo,               ///< Linear over the tempo range.
    StationVelocity,     ///< Requires `station`.
    StationProbability,  ///< Requires `station`.
    SceneRecall,         ///< Requires `scene`.
};

struct MidiMapping {
    MappingId id;
    MidiSource source;
    MappingTarget target = MappingTarget::GlobalTranspose;
    std::optional<StationId> station;
    std::optional<SceneId> scene;
    std::int32_t rangeSteps = 12;                  ///< GlobalTranspose span, 1–48.
};

struct MidiSettings {
    std::vector<MidiDeviceRef> outputs;            ///< D16: at most one in v1.
    bool virtualOutputPort = true;                 ///< macOS/Linux only; ignored on Windows.
    std::vector<MidiDeviceRef> inputs;
    bool sendClock = true;
    bool clockWhileStopped = false;                ///< D14
    bool songPositionBeforeStart = true;           ///< D14: SPP = 0 before Start.
    std::vector<MidiMapping> mappings;
};

// ---------------------------------------------------------------------------
// Project-level settings
// ---------------------------------------------------------------------------

enum class SeedMode : std::uint8_t { Fixed, NewEachPlay };
enum class SwingGrid : std::uint8_t { Eighths, Sixteenths };
enum class SceneQuantize : std::uint8_t { Immediate, NextBeat, NextFourBeats };

struct ProjectSettings {
    MilliBpm tempo;
    GridSpacing gridSpacing;
    PitchClass root;
    ScaleMask scale;
    std::uint64_t randomSeed = 1;
    SeedMode seedMode = SeedMode::NewEachPlay;
    std::vector<std::uint64_t> starredSeeds;       ///< D29
    std::int32_t swingPercent = 50;                ///< D36: 50–75; 50 = straight.
    SwingGrid swingGrid = SwingGrid::Sixteenths;
    std::uint32_t travelerCap = 8;                 ///< D23: 1–256.
    std::uint32_t activeNoteCap = 256;             ///< D23: 1–1024.
    SceneQuantize sceneQuantize = SceneQuantize::NextBeat;
    bool linkEnabled = false;                      ///< D37: runtime toggle; needs a Link build.
    std::uint32_t captureMinutes = 10;             ///< D28: 1–60.
};

struct ProjectMetadata {
    std::string name = "Untitled";
    std::string author;
    std::string notes;
};

struct ProjectDocument {
    ProjectSchemaVersion schemaVersion;
    ProjectMetadata metadata;
    ProjectSettings settings;
    MidiSettings midi;
    std::vector<Plot> plots;                       ///< D1: exactly one in v1.
    std::vector<Scene> scenes;                     ///< D35: at most 8.
};

/// A new, valid project: one empty 64×64 Plot and default settings.
[[nodiscard]] ProjectDocument makeDefaultProject();

// Limits referenced by validation and the UI.
namespace limits {
inline constexpr std::int32_t kMinPlotSize = 1;
inline constexpr std::int32_t kMaxPlotSize = 1024;
inline constexpr std::int64_t kMinNoteDuration = 1;
inline constexpr std::int64_t kMaxNoteDuration = 16 * kTicksPerQuarter;
inline constexpr std::int32_t kMaxRelativeSteps = 48;
inline constexpr std::int32_t kMaxRandomRange = 24;
inline constexpr std::int32_t kMaxWalkBound = 48;
inline constexpr std::size_t  kMaxChordTones = 6;
inline constexpr std::int32_t kMaxChordOffset = 24;
inline constexpr std::uint32_t kMaxAdditionalRepeats = 64;
inline constexpr std::int32_t kMaxHumanizeVelocity = 64;
inline constexpr std::int64_t kMaxHumanizeDelay = 480;
inline constexpr std::size_t  kMaxGateInputs = 16;
inline constexpr std::size_t  kMaxScenes = 8;
inline constexpr std::size_t  kMaxOutputs = 1;
inline constexpr std::uint32_t kMinTravelerCap = 1;
inline constexpr std::uint32_t kMaxTravelerCap = 256;
inline constexpr std::uint32_t kMinActiveNoteCap = 1;
inline constexpr std::uint32_t kMaxActiveNoteCap = 1024;
inline constexpr std::int32_t kMinSwing = 50;
inline constexpr std::int32_t kMaxSwing = 75;
inline constexpr std::uint32_t kMinCaptureMinutes = 1;
inline constexpr std::uint32_t kMaxCaptureMinutes = 60;
} // namespace limits

} // namespace signalflux
