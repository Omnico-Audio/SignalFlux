# Agent Completion Report

## Task
SF-006: Logic Gates and Terminators.

## Summary
`engine/LogicEngine` is a separate subsystem that turns boolean signals (Station Active, Switches, MIDI notes and controllers, other gates) into Station enable states. The Sequencer feeds it, evaluates it at the end of ticks whose inputs changed (D21 step 4) and immediately on external input, and consults it only for the silence decision. Terminators (D13) count Traveler entries per Station and end Travelers past the limit; limit 0 is Block.

## Acceptance status
| Criterion | Status / evidence |
|---|---|
| Gate graph evaluates continuously while stopped or playing | `Logic evaluates while stopped`; Switch/MIDI inputs evaluate immediately in any transport state |
| Gate output only changes Station mute state | `A false gate silences its Station but never changes routing (P2)` |
| Gates never redirect or delete Travelers | `Gates never redirect or delete Travelers, whatever they do`: identical routing with and without gates |
| Cycles have deterministic documented semantics | D12 feedback-edge rule; `A NOT gate fed back into itself toggles once per evaluation step`; `Cyclic logic during playback is deterministic`; golden log `gatekeeper` |
| Terminator count, scope, and reset semantics are explicit and tested | D13: counts every entry (silent too), per Station, reset on Play, kept on Continue and edits, runtime-only; five tests |

## Files changed
- New: `src/engine/LogicEngine.{h,cpp}`, `tests/unit/LogicTerminatorTests.cpp`, `tests/fixtures/golden/gatekeeper.log`, this report.
- Modified: `src/engine/Sequencer.{h,cpp}` (logic feed, Terminators, snapshot), `src/engine/ActiveNoteRegistry.{h,cpp}` (per-instance Station and release callback), `src/engine/EngineEvent.{h,cpp}` (`terminated` end reason), `src/domain/Examples.cpp` ("gatekeeper"), CMake lists, one registry test.

## Interfaces added or changed
- `LogicEngine` (`compile`, `step`, `stationEnabled`, `setSwitch`, gate accessors), `MidiInputState`.
- `Sequencer::setSwitch`, `setMidiNoteHeld`, `setMidiControl`.
- `EngineSnapshot::gates` (`GateView`) and `EngineSnapshot::stations` (`StationRuntimeView`: enabled, active, terminator count).
- `ActiveNoteRegistry::Release` now reports every instance with its Station and a `sendNoteOff` flag.

## Decisions made (within D12/D13)
- An evaluation step runs at the end of a processed tick if an input changed or the previous step changed an output, so feedback loops keep evolving while playing and stand still while nothing happens.
- External inputs (Switch, MIDI) run one step immediately at `now()`, consistent with D21 step 1 (edits before events).
- A Starter entering its Start Station counts toward that Station's Terminator.
- Gate outputs survive edits for gates that still exist.

## Tests run
| Command | Result |
|---|---|
| GCC 13.3 Debug / GCC 13.3 Release / Clang 18.1 Release, `-Werror`, `ctest` | 147/147 passed in each |

## Real-time / thread-safety review
- Engine thread only. `LogicEngine::step` does not allocate (fixed vectors sized at compile). `compile` allocates and runs only from `setDocument`.
- Logic cost per step is O(total gate inputs); steps happen only on changed ticks.
