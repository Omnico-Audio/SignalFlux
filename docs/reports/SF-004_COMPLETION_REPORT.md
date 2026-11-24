# Agent Completion Report

## Task
SF-004: Note Resolution (with the seeded random streams it needs).

## Summary
Pure note-resolution functions (`engine/NoteResolution.*`) and counter-based random streams (`engine/Random.h`), wired into the Sequencer. Each Traveler now carries explicit signal context: the previous resolved note (D19) and the last relative offset (D39). Context is copied to Instant children.

## Acceptance status
| Criterion | Status / evidence |
|---|---|
| Unit tests cover positive/negative intervals and scale boundaries | Round-trip test over all 13 presets × 12 roots × ±15 steps; octave tests; negative-pitch degree tests; folding test over −300…400 |
| Pass overrides Relative Values | `Pass borrows the carried note and overrides Relative Values` |
| Force Scale only quantizes absolute out-of-scale notes | Exhaustive test over all pitches/presets/roots; Relative output never passes through Force Scale (D11) |
| Unresolved edge cases are explicit policies | D9 (no previous note, out-of-scale reference), D10 (folding, Random Value Anchor/Walk), D11 (ties down, boundaries), D19, D34, D39, each with tests |

## Files changed
- New: `src/engine/Random.h`, `src/engine/NoteResolution.{h,cpp}`, `tests/unit/NoteResolutionTests.cpp`, `tests/fixtures/golden/wanderer.log`, this report.
- Modified: `src/engine/Sequencer.{h,cpp}` (context, resolution, seeds), `src/domain/Examples.cpp` ("wanderer"), `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/DECISIONS_AND_OPEN_QUESTIONS.md` (D39).

## Interfaces added or changed
- `stepInScale`, `forceToScale`, `foldToMidiRange`, `degreeIndex`, `pitchAtDegree`, `inScale`, `defaultReference`, `resolveNotes`, `NoteContext`, `ResolvedNotes`, `IntDraw`.
- `splitMix64`, `streamValue`, `RandomStreams` (`reset`, `remap`, `next`, `below`, `between`).
- `Sequencer::play(std::optional<uint64_t> seed)`, `Sequencer::currentSeed()`.

## Decisions made
- D39 (Relative Pass-through offset), recorded in DECISIONS.
- Resolution runs for silent Stations too, so mute never shifts melodies or random streams (P2, D19).
- Duplicate chord tones after folding collapse to one note.
- `IntDraw` is a virtual interface, not `std::function`, so the engine path cannot allocate.

## Tests run
| Command | Result |
|---|---|
| GCC 13.3 Debug, `-Werror`, `ctest` | 114/114 passed |
| Clang 18.1 Release, `-Werror`, `ctest` | 114/114 passed, including RNG golden value and all golden logs |

## Known limitations
- Random branching still behaves as Sequential until SF-005.

## Real-time / thread-safety review
- Threads touched: engine thread only.
- Allocations on deadline-critical paths: none. Resolution uses fixed arrays; `RandomStreams` allocates only in `reset`/`remap` (Play / edit).
- Locks / waits: none. Queue bounds: unchanged.
