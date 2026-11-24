# Behavioral Semantics

This document translates the supplied behavioral description into implementation rules.

> **Superseded where it conflicts:** `docs/DECISIONS_AND_OPEN_QUESTIONS.md` (accepted 2026-11-23) resolves every open decision below and is authoritative. Notably: distance is Manhattan route length (D2); in-flight edits preserve elapsed ticks (D3/D4); Arrival step 1 is removed because Note Off is scheduled at Note On (D20); relative values resolve from the previous *resolved* note (D19); a terminator limit of zero is a *Block* (D13).

## 1. Starter and Traveler lifecycle

### Start
When playback begins, every Station marked Start creates a Starter runtime object. The Starter immediately triggers its Station's event (or resolves a relative value), then begins counting musical ticks.

### Travel
Each engine tick advances the Starter's progress counter. The duration of a leg is:

`leg_duration_ticks = spatial_grid_distance(source, destination) * grid_spacing_ticks`

Default grid spacing is one quarter-note per grid cell.

The active leg's target duration is recalculated when the destination Station moves while the Traveler is in flight. Define the exact behavior for moving the source Station or editing a connection during flight as an explicit product decision and test it.

### Arrival
When progress reaches the current leg duration:
1. End the current note with Note Off, if one is active.
2. If Repeat is configured, perform the repeat behavior before advancing.
3. Evaluate the destination Station's enabled/muted state and probability.
4. Trigger / resolve the destination behavior.
5. Create the next Traveler leg toward the selected destination(s).

Avoid floating-point equality for tick arrival. Use integer tick positions or a defined fixed-point representation.

### Visual representation
- Traveler sprites are derived from engine runtime state.
- Traveler color comes from its source Station.
- The Path direction marker uses the same source color.
- Animation interpolates between engine-reported positions.
- A dropped UI frame must never change musical timing.

## 2. Branching modes

- **Sequential:** choose connected destinations round-robin.
- **Random:** choose one connected destination using a seedable random generator.
- **Instant:** trigger all connected destinations in the same scheduling instant.

Define whether round-robin state is per source Station, per Starter, or per connection set. Baseline recommendation: per source Station runtime state, reset behavior explicitly specified on playback restart.

## 3. Pass

Pass is a per-Station toggle.

When enabled, the Station borrows the last triggered event's note value carried along the signal chain. Pass overrides Relative Values for that Station. The Station still contributes its own applicable duration, velocity, channel, probability, repeat, and routing settings unless a separate rule says otherwise.

Treat the carried note as explicit runtime signal context, not as hidden mutable state on the visual Station.

## 4. Relative Values

A Station's note field represents a signed scale-step interval rather than an absolute MIDI note.

At trigger time:
- Resolve the interval relative to the previous Station's played note.
- Count only notes belonging to the configured Document Scale.
- Use Document Root and Document Scale.
- Support positive and negative intervals.
- Random Value Mode changes the reference to the Station's own stored value, as described in the supplied behavior notes.
- Relative Pass-through allows the incoming relative offset to skip the current Station's own relative value and continue to the next Station unchanged.

Open decisions requiring explicit tests:
- behavior when no previous played note exists;
- octave wrapping / MIDI range boundaries;
- behavior when the resolved note exceeds MIDI 0–127;
- precise interpretation of Random Value Mode and the stored value domain.

## 5. Force Scale

A Station's note field is an absolute MIDI note. When Force Scale is enabled and the note is outside the Document Scale, snap it to the nearest in-scale note.

- Force Scale is per Station.
- Document Root and Scale define the allowed pitch classes.
- If the note is already in scale, preserve it.
- Define deterministic tie-breaking when two in-scale notes are equally near.
- Define behavior at MIDI boundaries.

Relative Values and Force Scale are distinct modes. Do not apply Force Scale as a second quantization pass to a relative interval unless a future decision explicitly requires it.

## 6. Probability

Each Station has a probability from 0–100 percent. On arrival, evaluate whether the Station fires. Use deterministic seeded randomness in tests. Specify whether a failed probability check still allows the Traveler to continue; baseline interpretation: it does not erase the chain, and progression continues according to the Station's configured connection behavior, but this must be confirmed in a behavior test.

## 7. Repeat

Repeat re-triggers the same note N times at the same duration before the Traveler passes onward.

Implement repeat scheduling as explicit note events with guaranteed Note Off handling. Clarify whether N means total triggers or additional re-triggers; do not infer silently. Use a named parameter such as `additionalRepeats` once confirmed.

## 8. Logic gates

Logic gates (AND, OR, XOR, NOT, etc.) only mute or unmute Stations.
- They do not block or redirect Travelers.
- They do not choose destinations.
- Logic evaluation is continuous and independent of Play/Stop.
- Gate evaluation is isolated from event scheduling.
- The engine publishes the resulting Station mute state to the sequencer.

Define the exact gate input model, evaluation cadence, and cycle handling. For cyclic gate graphs, choose deterministic semantics (e.g. staged evaluation / previous-cycle values) and test them.

## 9. Terminators

Terminators allow a configured number of Travelers through before stopping the chain.
- A limit of zero means permanent mute.
- The count and reset semantics must be defined.
- Distinguish a chain termination from Station muting.
- Ensure that terminator state is deterministic across save/load and playback restart.

## 10. Required edge cases

- Stop while notes are active.
- Panic while multiple channels are active.
- Station moved during a Traveler leg.
- Connection deleted during a leg.
- Station muted/unmuted while a Traveler is approaching.
- Device disconnected while notes are active.
- Probability rejects an arrival.
- Multiple same-pitch notes on the same channel overlap.
- Repeat interrupted by Stop or Panic.
- Project replaced while playback is active.
