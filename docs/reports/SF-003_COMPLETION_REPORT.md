# Agent Completion Report

## Task

SF-003: Runtime Graph and Scheduler.

## Summary

Implemented the sequencing core: a compiled, immutable `RuntimeGraph`; an event-driven integer-tick `Sequencer` with Starters, Traveler legs, Sequential and Instant branching, an active-note registry, Traveler and note caps, in-flight edits, and Play/Stop/Continue/Panic. All engine output is one ordered event stream with a canonical text form. `signalflux-render` now renders built-in example projects, and golden logs for them are compared byte for byte in CI on every platform (the D25 gate).

## Clarifications identified before implementation

The spec left these open; each is resolved by an accepted decision, and the implementation follows it:

| Question | Resolution |
|---|---|
| What happens to a Traveler when geometry changes mid-flight? | D3/D4, isolated in `engine/LegTiming.h` |
| Connection or Station deleted mid-flight? | D5: Traveler ends; its note still ends on schedule |
| Same-tick ordering? | D21: edits → Note Offs → arrivals by TravelerId |
| How is "now" defined between host calls? | `processUntil(h)` is half-open: processes ticks `< h`, leaves `now() == h`. Edits apply at `now()` before its events |
| Stop granularity | D14: next MIDI Clock boundary (40 ticks) |
| Which Traveler continues after Instant branching? | The arriving Traveler takes the first route in outgoing order and keeps its id; extra routes get new ids in order (D21) |

## Acceptance status

| Criterion | Status | Evidence |
|---|---|---|
| Start Stations spawn Starters on Play | Done | `Each Start Station creates one Starter on Play, in StationId order` |
| Immediate first trigger | Done | `A Starter triggers immediately at tick 0` |
| Integer tick scheduling | Done | `MusicalTick` (int64) throughout; no floating point in `src/engine` |
| Path distance/grid spacing timing | Done | `Leg duration = Manhattan distance x grid spacing`, runtime-graph tests |
| Destination movement policy is isolated and unit-tested | Done | `engine/LegTiming.h` + 5 policy sections; D3/D4 integration tests |
| UI frame rate has no effect on event deadlines | Done | `Event timing is independent of how processUntil is chunked`: 1-tick, ~60 fps, irregular and single-shot runs produce identical logs |

ACCEPTANCE_AND_TEST_PLAN §2 items covered now: Starter creation, immediate emit, path timing, destination moves, visuals not affecting timing, Sequential round-robin, Instant same-instant triggering. The rest of §2 belongs to SF-004–SF-006.

## Files changed

New:
- `src/engine/LegTiming.h`
- `src/engine/RuntimeGraph.{h,cpp}`
- `src/engine/EngineEvent.{h,cpp}`
- `src/engine/ActiveNoteRegistry.{h,cpp}`
- `src/engine/Sequencer.{h,cpp}`
- `src/engine/EventLog.{h,cpp}`
- `src/domain/Examples.{h,cpp}`: four built-in examples
- `tests/unit/{TestProjects.h, EngineComponentTests.cpp, SequencerTests.cpp, GoldenLogTests.cpp}`
- `tests/CompareRenderOutput.cmake`
- `tests/fixtures/golden/{first-loop, sequential-hub, instant-bloom, polyrhythm}.log`
- `.gitattributes`: golden logs are never line-ending converted
- this report

Modified:
- `src/CMakeLists.txt`: engine sources
- `src/render/main.cpp`: `--list`, `--example NAME [--beats N]`, binary stdout on Windows
- `tests/CMakeLists.txt`: new tests, golden directory, renderer output tests

## Interfaces added

- `engine::recomputeDeadline(legStart, newDuration, now)`
- `engine::RuntimeGraph::compile(doc, report*)`, with Station/Flowpoint lookups and Start Stations in id order
- `engine::EngineEvent`, `IEngineEventSink`, `toCanonicalString()`
- `engine::ActiveNoteRegistry`: `reset`, `setCapacity`, `add`, `nextDue`, `releaseDue`, `releaseAll`
- `engine::Sequencer`: `setDocument`, `play`, `requestStop`, `continuePlayback`, `panic`, `processUntil`, `snapshot`, `stats`
- `engine::EventLog`, `renderCanonicalLog(doc, length)`
- `examples::names()`, `examples::make(name)`

## Decisions made

- **Edits are whole-document replacements** (`setDocument`), reconciled against running Travelers. This covers every edit kind with one code path. Finer-grained commands can come with undo/redo (D27) without changing the reconciliation rule.
- **Event-driven scheduling**: the engine jumps between due events rather than stepping every tick. Same results, much less work, and chunk-size independence by construction.
- **`processUntil` does nothing while stopped**, so engine time freezes and Continue resumes exactly.
- **Play while playing** releases sounding notes first, then restarts.
- **A dropped Traveler has no id** (`t=0` in the log), so dropping does not shift later ids.
- **Interim behavior, replaced by later tasks**: Random branching acts as Sequential (SF-005); notes are absolute only (SF-004); CC and Pitch Bend Stations emit nothing (SF-007). Golden logs only cover patches whose behavior is final.

## Tests run

| Command | Result |
|---|---|
| GCC 13.3 Debug, `-Werror`: build + `ctest` | 89/89 passed (83 test cases, 1250 assertions + renderer tests) |
| Clang 18.1 Release, `-Werror`: build + `ctest` | 89/89 passed. Golden logs generated with GCC Debug match byte for byte |
| Renderer comparison with a deliberately different length | Fails as expected |
| macOS / Windows | Run by CI on the pull request |

## Known limitations

- Engine is not yet driven by a real clock or connected to MIDI output; that is SF-007.
- No logic gates or Terminators yet (SF-006); the "logic" step of D21 is a no-op.
- `setDocument` recompiles the graph and may allocate. It runs on the engine/control thread, not the MIDI/audio callback. `processUntil` does not allocate after the first Play.
- `ActiveNoteRegistry::releaseDue` uses linear scans over at most 1024 entries. Fine at current caps; revisit if profiling (ACCEPTANCE §6) says otherwise.

## Follow-up tasks

1. SF-004: note resolution (Relative, Random Value with Anchor/Walk, Pass, Force Scale, chords, carried note context).
2. SF-005: counter-based RNG streams (D10), weighted Random branching (D32), probability (D7), Repeat (D8).
3. SF-006: logic gates and Terminators, plugged into D21 step 4 and the arrival path.
4. SF-007: clock thread driving `processUntil`, tick → time conversion, MIDI output from the event stream.

## Real-time / thread-safety review

- Threads touched: none at runtime yet. `Sequencer` is documented as single-thread-owned (the engine thread).
- Allocations on deadline-critical paths: none in `processUntil` after the first `play()`. Traveler storage is reserved at the 256 maximum, the note registry at 1024, and route scratch at the largest outgoing count. Sorting uses `std::sort` on reserved storage.
- Locks / waits: none.
- Queue bounds and overflow behavior: Traveler cap (default 8) and active-note cap (default 256) with counted drops (D23). Note Offs are never dropped.
- Sinks: `IEngineEventSink::onEngineEvent` is `noexcept`. `EventLog` allocates and is test/offline-only; the real-time MIDI sink (SF-007) must be a bounded queue.
