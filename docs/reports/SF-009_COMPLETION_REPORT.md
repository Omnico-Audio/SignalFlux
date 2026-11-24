# Agent Completion Report

## Task
SF-009: Grid UI and Traveler Visualization, plus the approved beta features that live in the UI: undo/redo (D27), Capture (D28), seed history (D29), MIDI Learn (D30), beat ticks (D31), Scenes (D35) and the theme toggle (D40).

## Summary
The test harness is replaced by the real editor: toolbar, scrollable Plot canvas, property inspector and Scenes bar. All edits go through `AppState` as pure document transforms from the new `domain/Editing` module, are validated, become undo steps, and reach the engine as immutable snapshots. The canvas draws Travelers from engine snapshots only.

## Acceptance status
| Criterion | Status / evidence |
|---|---|
| Visual Travelers mirror engine state | Drawn from `EngineSnapshot` (leg start, deadline, tick); interpolation is presentation-only |
| Traveler color comes from source Station | `GridCanvas::drawTravelers` uses the source Station's palette colour |
| Direction marker uses source color | Route stroke and mid-route arrow use the source Station's colour |
| UI frame drops do not alter sequencing | The UI never drives time; engine chunk-invariance test (SF-003) proves timing is independent of how often it runs |
| Station movement sends model commands, never mutates engine memory | Canvas calls `AppState::edit` → `editing::moveStation` → `EngineHost::setDocument`; engine applies D3/D4 |

Feature coverage: undo/redo with gesture coalescing (tested); Capture to `.mid` (writer tested; UI button); seed history with recall, lock and stars (persisted in app settings and project); MIDI Learn for tempo, transpose, Station velocity and probability (router tested; inspector buttons); beat ticks from the engine's own leg-duration function (tested); Scenes store/recall with beat quantization (tested in core; host applies on the beat); Night transit / Paper sketch theme toggle saved as a preference.

## How to use (beta)
- **Station tool:** click a cell to add a Station (the first one is the Start). **Select tool:** drag to move; **Shift-drag** from one Station to another to connect; double-click a route to flip its corner; Delete removes the selection.
- **Gate / Switch tools:** click to place. Wire gate inputs and target Stations in the inspector. Click a Switch on the canvas to flip it.
- **Space** play/stop, **Esc** panic, **Cmd/Ctrl+Z** undo, **Shift+Cmd/Ctrl+Z** redo, **Cmd/Ctrl+S** save.
- **Scenes:** Shift-click a slot to store, click to recall (on the next beat by default).

## Files changed
- New: `src/domain/Editing.{h,cpp}`, `src/domain/RouteGeometry.h`, `src/midi/MidiFileWriter.{h,cpp}`, `src/app/{Theme.h, AppState.{h,cpp}, GridCanvas.{h,cpp}, Inspector.{h,cpp}}`, `tests/unit/EditingTests.cpp`, this report.
- Modified: `src/app/MainComponent.{h,cpp}` (full layout), `src/app/Main.cpp` (title, save-before-quit), `src/render/main.cpp` (`--export-midi`), validation and logic (unwired gates are allowed and stay on), CMake lists.

## Decisions made
- History stores whole documents (bounded to 500 steps): simple and exactly correct for projects of this size.
- Switch flips and Scene recalls replace the current history step instead of adding one (D27). Undoing an earlier structural edit restores that step's Switch states.
- A gate with no inputs is valid (warning) and outputs on, so new gates and deleted references never silence Stations unexpectedly.
- The first Station added to an empty project is the Start Station.
- Route order for Sequential branching is creation order; reordering in the UI is a follow-up.

## Tests run
| Command | Result |
|---|---|
| GCC 13.3 Debug, `ctest` | 209/209 passed |
| All app sources compiled against JUCE 9.0.6 + Link on Linux | Compiled; link and app bundle built by CI on macOS and Windows |

## Known limitations (needs hands-on testing)
- The UI has never been run here (no display). Layout, interaction and performance need checking on macOS and Windows.
- Swing and humanize (D36) have project fields but no engine support or UI yet.
- One MIDI output and no MIDI input device picker in the toolbar yet (inputs can be opened through `EngineHost::selectInputs`).
