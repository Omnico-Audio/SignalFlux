# SF-010 — Persistence, Integration, Release

Implement project save/load, schema versioning, autosave/recovery, packaging, and cross-platform release validation.

Acceptance:
- Round-trip project tests pass.
- Missing devices do not block loading.
- Loading never unexpectedly starts MIDI.
- Installable application packages produced for all target OSes.
- Hardware / virtual MIDI validation recorded.
