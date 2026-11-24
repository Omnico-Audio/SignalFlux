# SF-002 — Domain Model

Implement strong types and serializable domain structures for Plots, Stations, Flowpoints, scale/root, branching, logic gates, terminators, MIDI settings, and project metadata.

Acceptance:
- Domain has no UI dependency.
- Validate MIDI channels 1–16, notes 0–127, velocity, probability 0–100.
- Graph references are validated.
- Serialization is not coupled to UI component state.
