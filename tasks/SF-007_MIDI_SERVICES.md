# SF-007 — MIDI Services

Implement cross-platform MIDI input/output adapters, channel handling, clock generator, CC ramps, Pitch Bend, active-note registry, and Panic.

Acceptance:
- All 16 channels.
- MIDI Clock at 24 pulses per quarter note.
- Stop / Continue / Start behavior documented.
- MIDI input modulation while running.
- Panic silences all channels.
- Device disconnect handled safely.
- Synth is not required for MIDI output.
