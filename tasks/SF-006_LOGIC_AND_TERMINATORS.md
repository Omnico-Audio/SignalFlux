# SF-006 — Logic Gates and Terminators

Implement logic evaluation as a separate subsystem.

Acceptance:
- Gate graph evaluates continuously while stopped or playing.
- Gate output only changes Station mute state.
- Gates never redirect or delete Travelers.
- Cycles have deterministic documented semantics.
- Terminator count, scope, and reset semantics are explicit and tested.
