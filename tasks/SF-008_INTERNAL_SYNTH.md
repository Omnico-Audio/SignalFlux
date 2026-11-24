# SF-008 — Optional 16-Voice Synth

Implement a lightweight internal monitoring synth.

Acceptance:
- 16 voices.
- Deterministic voice-stealing.
- Uses the same scheduled events as external MIDI.
- Disable synth and verify external MIDI output is unchanged.
- Audio callback is bounded and allocation-free.
