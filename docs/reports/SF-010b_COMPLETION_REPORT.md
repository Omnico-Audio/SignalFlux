# Agent Completion Report

## Task
SF-010 (part 2 of 2): autosave/recovery, packaging, release validation.

## Summary
Autosave and recovery shipped with the UI in SF-009 (`AppState`): the project is autosaved every 60 s while it has unsaved changes, a clean exit deletes the recovery file, and on the next launch after a crash the app offers to restore it, stopped. This part adds the **Release** workflow, which builds, tests and packages a macOS `.dmg` (Apple Silicon, ad-hoc signed) and a Windows `.zip`. Each package contains the app, the example projects, the licence and a "READ ME FIRST" guide. The workflow publishes a GitHub pre-release when a `v*` tag is pushed. A release checklist and a hardware validation record template complete the task.

## Acceptance status
| Criterion | Status |
|---|---|
| Round-trip project tests pass | Done (part 1) |
| Missing devices do not block loading | Done (part 1); the UI falls back to the virtual port or "none" when a saved device is missing |
| Loading never unexpectedly starts MIDI | Open and recovery stop the engine and never call Play |
| Installable application packages for all target OSes | Release workflow produces macOS `.dmg` and Windows `.zip`. Not notarized or code-signed (needs Apple Developer ID and a Windows certificate) |
| Hardware / virtual MIDI validation recorded | Template in `docs/HARDWARE_VALIDATION.md`; **no results yet**, which needs your hardware |

## Files changed
- New: `.github/workflows/release.yml`, `packaging/README-beta.txt`, `docs/RELEASE_CHECKLIST.md`, `docs/HARDWARE_VALIDATION.md`, this report.
- Modified: `README.md`.

## Known limitations
- Unsigned builds trigger Gatekeeper and SmartScreen warnings; READ ME FIRST explains how to open them.
- Apple Silicon only on macOS.
