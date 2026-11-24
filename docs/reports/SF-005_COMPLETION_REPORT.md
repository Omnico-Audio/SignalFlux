# Agent Completion Report

## Task
SF-005: Branching, Probability, Repeat.

## Summary
The sequencer was restructured around one "enter Station" step: silence decision (manual mute, probability; gates join in SF-006), resolution, Note Ons, then either a Repeat group (the Traveler dwells at the Station) or departure. Weighted Random branching uses the Station's seeded Branch stream.

## Acceptance status
| Criterion | Status / evidence |
|---|---|
| Random source is seedable | Counter-based streams (SF-004); `Random branching is reproducible for a seed and differs across seeds`; `Probability is reproducible for a seed` |
| Sequential state policy is explicit | D6: per source Station, explicit outgoing order, reset on Play, kept on Continue and across edits (SF-003 tests) |
| Repeat generates correctly paired Note On/Off events | `additionalRepeats = 2 plays 3 times...`: 3 On/Off pairs; Note Off before Note On at shared ticks; Stop and Panic tests show no stuck notes |
| Probability failure semantics are explicit and tested | D7: `A failed probability check is a silent pass-through: same timing and routing`; exact endpoints at 0 and 100 |

## Files changed
- Modified: `src/engine/Sequencer.{h,cpp}` (rewritten around enter-Station / dwell / depart), `src/engine/RuntimeGraph.{h,cpp}` (probability, repeats, weights, terminator and CC/PB fields), `src/engine/EngineEvent.{h,cpp}` (`Repeat` event), `src/domain/Examples.cpp` ("branches"), `tests/CMakeLists.txt`, `docs/DECISIONS_AND_OPEN_QUESTIONS.md` (D40 theme).
- New: `tests/unit/BranchProbabilityRepeatTests.cpp`, `tests/fixtures/golden/branches.log`, this report.

## Decisions made
- Repeat triggers replay the notes resolved on arrival (no re-resolution or extra random draws).
- Stop/Panic during a Repeat group silence the remaining triggers but keep the dwell time, so routing and timing never depend on transport interruptions beyond the Stop itself (P2, D8).
- Branch choice happens at departure (after any Repeat group).
- Snapshot marks dwelling Travelers (`dwelling = true`, `from == to`) for the UI.

## Tests run
| Command | Result |
|---|---|
| GCC 13.3 Debug, `-Werror`, `ctest` | 130/130 passed |
| GCC 13.3 Release (`-O3`), `-Werror`, `ctest` | 130/130 passed |
| Clang 18.1 Release, `-Werror`, `ctest` | 130/130 passed |

All earlier golden logs are byte-identical after the restructure.

## Real-time / thread-safety review
- Engine thread only. No new allocation on `processUntil`: dwell state lives in the preallocated Traveler records; branch draws use the bounded integer sampler.
