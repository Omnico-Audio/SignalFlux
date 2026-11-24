# Agent Completion Report

## Task
SF-010 (part 1 of 2): project save/load, schema versioning, example projects. Part 2 (autosave/recovery in the app, packaging, release validation) follows with the app work.

## Summary
New `signalflux_persistence` library: canonical JSON writer, strict-but-lenient reader that collects every error with a path, migration chain for older schemas, refusal of newer schemas, validation before returning, and atomic save. All seven built-in examples are committed as `.sflux` files in `examples/`, and `signalflux-render --project FILE` renders them; CI checks the file-based logs against the same golden logs. Stations gained a palette colour index (D40) before the format was fixed.

## Acceptance status (this part)
| Criterion | Status / evidence |
|---|---|
| Round-trip project tests pass | `Round trip preserves every field` (every field non-default, including 64-bit seeds, unicode and newlines); every example file round-trips |
| Missing devices do not block loading | `Missing MIDI devices never block loading` |
| Loading never unexpectedly starts MIDI | Documents carry no transport state; loading returns data only (test documents the guarantee). App-side enforcement lands with autosave/recovery |
| ACCEPTANCE §5: unknown future schema fails gracefully | `Unknown future schema versions fail gracefully` |
| ACCEPTANCE §5: older schema migration covered by fixtures | `Older schemas are upgraded through the migration chain` (registry mechanism; schema 1 is the first real version) |
| ACCEPTANCE §5: corrupt project reports a useful error without partial mutation | `Corrupt or foreign files...`, `A structurally invalid document is refused...`, `Saving into a missing folder fails without touching anything` |
| Installable packages, hardware validation | Part 2 |

## Files changed
- New: `src/persistence/ProjectSerializer.{h,cpp}`, `tests/unit/PersistenceTests.cpp`, `examples/*.sflux` (7), this report.
- Modified: `src/domain/Project.h` (defaulted equality on all document types; `Station::colour`), `src/domain/Validation.cpp` (colour range), `src/domain/Examples.cpp` (colours), `src/render/main.cpp` (`--project`, `--export-examples`), CMake files (persistence library, pinned nlohmann/json 3.12.0, file-based render tests), `tests/CompareRenderOutput.cmake`, `.gitattributes`.

## Decisions made
- Enums are written as strings, ids and musical values as integers, seeds as decimal strings.
- Missing optional keys take defaults (hand-written files stay easy); wrong types or out-of-range values are errors with a JSON path. Unknown keys are ignored.
- `MigrationRegistry` is an immutable value passed to the loader (no global mutable state); the shipped registry is empty because schema 1 is the first version.
- nlohmann/json headers are included as system headers so strict warnings stay on for SignalFlux code.

## Tests run
| Command | Result |
|---|---|
| GCC 13.3 Debug / GCC 13.3 Release / Clang 18.1 Release, `-Werror`, `ctest` | 167/167 passed in each |

## Real-time / thread-safety review
Persistence runs on the message thread or a background thread, never on the engine or audio path. Saving allocates and performs disk I/O by design.
