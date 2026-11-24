# Architecture and Interface Contracts

## 1. High-level structure

```text
SignalFluxApp
 ├── Project / Document Model
 ├── UI
 │    ├── GridCanvas
 │    ├── StationEditor
 │    ├── LogicEditor
 │    ├── TransportPanel
 │    └── TravelerRenderer
 ├── Sequencing Core
 │    ├── RuntimeGraph
 │    ├── StarterManager
 │    ├── TravelScheduler
 │    ├── BranchResolver
 │    ├── NoteResolver
 │    └── ActiveNoteRegistry
 ├── Logic Engine
 │    ├── GateGraph
 │    └── StationMuteState
 ├── MIDI Services
 │    ├── MidiInputManager
 │    ├── MidiOutputManager
 │    ├── ClockGenerator
 │    └── MidiEventQueue
 ├── Audio
 │    └── Optional 16-Voice Synth
 └── Persistence
      ├── ProjectSerializer
      └── MigrationRegistry
```

## 2. Dependency rules

- Domain types must not depend on JUCE GUI classes.
- Sequencing core must be testable without JUCE audio devices.
- MIDI device adapters depend on JUCE MIDI APIs but expose application-owned interfaces.
- UI reads immutable snapshots / message-thread notifications and sends commands.
- Audio callback consumes prebuilt event/audio data and does not traverse the mutable project graph.
- Persistence serializes domain state, not UI component objects.

## 3. Suggested repository

```text
SignalFlux/
  CMakeLists.txt
  CMakePresets.json
  cmake/
  external/
  src/
    app/
    domain/
    engine/
    logic/
    midi/
    audio/
    persistence/
    ui/
  tests/
    unit/
    integration/
    fixtures/
  docs/
  assets/
  packaging/
```

## 4. Key domain types

Use strong types rather than ambiguous integers:
- `StationId`, `PlotId`, `ConnectionId`
- `MidiChannel` (validated 1–16)
- `MidiNote` (validated 0–127)
- `Velocity` (validated 1–127 for Note On; zero semantics explicitly handled)
- `MusicalTick`
- `GridCoordinate`
- `GridSpacing`
- `ProbabilityPercent`
- `TravelerId`
- `ProjectSchemaVersion`

## 5. Core interfaces (illustrative contracts)

```cpp
struct EngineCommand;
struct EngineSnapshot;
struct ScheduledMidiEvent;

class ISequencingEngine {
public:
    virtual ~ISequencingEngine() = default;
    virtual void submit(const EngineCommand&) = 0;
    virtual EngineSnapshot snapshot() const = 0;
    virtual void processUntil(MusicalTick horizon) = 0;
};

class IMidiOutput {
public:
    virtual ~IMidiOutput() = default;
    virtual bool send(const ScheduledMidiEvent&) noexcept = 0;
    virtual void panicAllChannels() noexcept = 0;
};

class IProjectStore {
public:
    virtual ~IProjectStore() = default;
    virtual bool save(const ProjectDocument&, const File&) = 0;
    virtual ProjectLoadResult load(const File&) = 0;
};
```

These signatures are conceptual. Agents must define thread ownership, queue capacity, error handling, and whether snapshot access is lock-free before implementing.

## 6. Scheduling and timing

- Use integer musical ticks as the canonical sequencing time.
- Define ticks per quarter note centrally (e.g. 960 PPQ internally).
- Convert musical tick deadlines to monotonic timestamps / audio sample offsets at the output boundary.
- MIDI Clock: 24 pulses per quarter note.
- Preserve fractional conversion remainder to avoid long-term drift.
- Handle tempo changes without discontinuity or stuck notes.
- Queue events crossing the current audio/device block boundary.
- UI interpolation is presentation-only.

## 7. Real-time and concurrency model

Suggested ownership:
- Message thread: UI and user commands.
- Engine/control thread: graph edits, logic evaluation, project mutation, runtime state preparation.
- MIDI output worker / platform callback: device-specific dispatch as supported by platform API.
- Audio callback: optional synth rendering and bounded event consumption.

Use bounded single-producer/single-consumer queues where ownership permits. If multiple producers exist, use a bounded MPSC design with documented contention behavior. Do not assume a generic concurrent queue is real-time safe.

Project edits should produce a validated immutable runtime graph or staged command updates. Reclaim old graph snapshots only when no real-time reader can access them.

## 8. MIDI note lifecycle

Maintain an active-note registry keyed by internal note identity and including channel, pitch, originating Station / Traveler, and scheduled end. MIDI 1.0 Note Off does not carry an internal note ID; overlapping notes with identical channel and pitch require deterministic release semantics.

Rules:
- Every accepted Note On must have a planned Note Off.
- Stop and Panic flush active notes.
- Note Off and Panic are prioritized over non-critical CC updates if output buffers overflow.
- Changing a Station's channel during playback must not strand an already sounding note; release it using the channel/pitch identity captured at Note On.
- Panic sends the appropriate all-notes-off / all-sounds-off safety messages across all 16 channels and clears internal state.

## 9. MIDI clock and output

- Generate 24 MIDI Clock pulses per quarter note.
- Define Start, Continue, Stop, and Song Position Pointer semantics for user transport actions.
- Clock generation must share the same tempo authority as the sequencer.
- MIDI device selection and reconnect behavior must be resilient.
- Support selecting multiple outputs only if the platform/device model can guarantee safe dispatch; otherwise begin with one selected output and document it.

## 10. Optional synthesizer

- 16-voice polyphony.
- Synth receives the same scheduled note events as MIDI output.
- Voice stealing policy is deterministic and tested.
- Synth disabled must not change external MIDI output.
- Audio device failure must not prevent MIDI sequencing.

## 11. JUCE / CMake implementation

- Use JUCE as a pinned dependency, preferably a git submodule or reproducible FetchContent declaration pinned to a release tag/commit.
- Use CMake as the only project generator/build definition.
- Create a `juce_add_gui_app` target for the desktop application.
- Do not create plugin targets.
- Keep platform MIDI differences behind `IMidiInput` / `IMidiOutput` adapters.
- Check JUCE's current licence terms before distribution; the framework's licensing model must be reviewed against the intended commercial/open-source distribution model.
