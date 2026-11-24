# Decisions and Open Questions

**Status:** Accepted by product owner, 2026-11-23
**Applies to:** SignalFlux Greenfield Developer Pack v2.0

Each decision below has a rule, the reasoning, and the tests that pin it down. All decisions are accepted and are authoritative alongside `docs/`. Where this file and an older spec document disagree, this file wins until the spec edits listed at the end are applied.

## Terminology change: Point → Station

The musical node formerly called a **Point** is now a **Station**. Travelers depart from and arrive at Stations, which fits the travel metaphor and removes the Plot/Point confusion. Every identifier, document, and UI string uses the new name:

| Old | New |
|---|---|
| Point | Station |
| Start Point | Start Station |
| `PointId` | `StationId` |
| Point mute state | Station mute state |

Plot (the canvas), Flowpoint / Path (the directed connection), Traveler, Starter, Terminator and Block are unchanged.

---

## Governing principles (derived from the spec, used to settle the questions)

These four rules resolve most of the questions consistently. If the owner accepts them, the individual decisions follow.

- **P1 — Geometry is time.** Leg duration is always the *current* integer length of the connection, in grid cells, multiplied by grid spacing in ticks. The line on screen is the clock.
- **P2 — Mute and probability affect sound, never timing or routing.** A silent Station still resolves, still waits through its repeats, and still sends Travelers onward. (This is required by "gates never redirect or delete Travelers": if mute stopped Travelers, gates would delete them.)
- **P3 — Only Terminators and deleted connections end Travelers.** Ending a chain is a routing act with its own explicit causes.
- **P4 — Everything is integer and ordered.** Integer ticks, integer cells, an explicit event order within a tick, and per-Station seeded random streams. No floating-point equality, no ordering by container iteration.

---

## Confirmed baseline (unchanged)

- Product name: SignalFlux.
- JUCE + CMake.
- Standalone desktop application, not a DAW or plugin.
- Windows, macOS, Linux.
- Grid-based, non-linear sequencing.
- MIDI output up to 16 channels.
- MIDI Clock required.
- Optional internal 16-voice synthesizer.
- Travelers are visual only; engine computes timing.
- Logic gates mute/unmute Stations only.
- At least 16 concurrent Travelers as a minimum target.

---

## D1 — What is a Plot?

[x] Accepted

**Decision.** A Plot is the canvas: a bounded grid containing Stations, connections, gates, and their layout. **A v1 project contains exactly one Plot.** The schema stores `plots` as an array so that multi-Plot projects can be added later without a migration, but v1 load rejects arrays of any other length with a clear error.

Grid rules:
- Stations sit on integer cell coordinates `(x, y)`, `x, y >= 0`.
- Default Plot size 64 × 64 cells, configurable.
- At most one Station per cell (validation error otherwise). Minimum leg length is therefore 1 cell.

**Spec fix required.** `README.md` and `PRODUCT_AND_SYSTEM_SPEC.md §1` say users "place Plots on a grid and connect them with Flowpoints". That contradicts the glossary in §3, where Plot is the canvas and Station is the node. Change the prose to "place Stations on a Plot's grid and connect them with Flowpoints."

**Tests.** Two Stations in one cell fail validation; a project with `plots.length == 2` fails load with a schema error, not a crash.

---

## D2 — Flowpoint geometry and distance

[x] Accepted

**Decision.** Connections are drawn as **orthogonal routes** (horizontal and vertical segments along grid lines). Distance is the **Manhattan length of the route in cells**.

```
leg_length_cells  = |dx| + |dy|                      (v1: single elbow)
leg_duration_ticks = leg_length_cells * grid_spacing_ticks
```

- Default route: one elbow, horizontal-first. Per-connection toggle for vertical-first. Both routes have the same length, so the toggle is purely visual.
- Schema reserves `waypoints: []` on each connection. A future version may allow orthogonal waypoints. The length is then the sum of segment lengths, and the formula and all timing rules stay unchanged.
- Self-connections (a Station to itself) are invalid in v1; use Repeat instead.

**Why Manhattan with orthogonal routes:**

| Option | Duration for (3, 2) offset | Integer? | Drawn length matches time? | Verdict |
|---|---|---|---|---|
| Euclidean, straight line | 3.606 cells | No: needs rounding, so the sprite and the clock disagree | Yes, before rounding | Reject: non-integer durations break rhythmic intuition and P4 |
| Chebyshev (diagonal = 1), straight line | 3 cells | Yes | No: diagonals look longer but take the same time, so sprite speed varies by angle | Viable alternative |
| **Manhattan, orthogonal route** | **5 cells** | **Yes** | **Yes: constant sprite speed of one cell per grid step** | **Recommended** |
| Free routed path | Arbitrary | Only if snapped | Yes | Defer: adds editing complexity; waypoints above give an upgrade path |

With Manhattan and orthogonal routes, a musician can count cells along the line and know the rhythm. Every Traveler moves at the same visible speed, so the animation honestly reflects timing. The Traveler renderer also becomes trivial: position = elapsed cells along a polyline.

Chebyshev with straight diagonal lines was considered and rejected by the owner.

**Tests.** (0,0)→(3,2) = 5 × 960 = 4800 ticks at default spacing; route-direction toggle does not change duration; grid spacing 1/8 halves all durations.

---

## D3 — Destination Station moves during a leg

[x] Accepted

**Decision (P1).** Elapsed ticks are preserved; the leg's total length is recomputed from the new geometry.

```
deadline = leg_start_tick + current_length_cells * current_grid_spacing_ticks
if deadline <= now: arrive at now (the tick the edit is applied)
```

- Edits reach the engine as commands applied at a tick boundary. Because Stations snap to cells, only a cell change produces a command; dragging within a cell does nothing.
- No retroactive arrivals: if the new length has already been exceeded, the Traveler arrives on the tick the edit is applied.
- Sprite position = `min(elapsed_ticks / grid_spacing_ticks, length)` cells along the current route, so the sprite may jump when the route changes. That is correct: it shows the truth.

**Rejected alternatives.** Restarting the leg from zero punishes small edits with long delays. Keeping fractional progress produces non-integer remainders.

**Tests.** Lengthen during flight: arrival moves later by exactly the added cells × spacing. Shorten below elapsed: arrival occurs at the edit tick. Edit applied at the same tick as the original deadline: arrival happens once, not twice.

---

## D4 — Source Station moves during a leg

[x] Accepted

**Decision.** The same rule as D3. A leg's length is always the current length of its connection, whichever endpoint moved. Elapsed ticks are preserved. The sprite is re-projected onto the new route.

This gives one rule covering source moves, destination moves, route toggles, future waypoint edits, and grid-spacing changes.

**Tests.** Move source farther away mid-flight: arrival later by the added cells × spacing; elapsed is unchanged.

---

## D5 — Connection deleted while a Traveler is on it

[x] Accepted

**Decision (P3).** The Traveler is removed at the tick the deletion is applied. No arrival, no trigger, no onward travel.

- Notes already sounding are owned by the active-note registry and end at their scheduled Note Off. Deleting a connection never cuts or strands a note.
- Deleting a Station deletes its connections, so the same rule applies.
- Recreating the connection does not revive the Traveler.
- Adding a connection mid-flight affects only future branching decisions.

**Tests.** Delete mid-leg: no arrival event, the prior note still receives its Note Off on schedule, and the Traveler count decrements.

---

## D6 — Sequential branching state

[x] Accepted

**Decision.** Round-robin state is **per source Station**, shared by every Traveler departing from it.

- Each Station stores an explicit, user-reorderable **outgoing order** for its connections. New connections are appended. Order is persisted. Never order by `ConnectionId` or container iteration.
- The counter holds the index of the next connection. It advances on every departure, including silent ones (P2).
- After a deletion, if `index >= count`, reset to 0.
- Reset to 0 on **Play**. Preserved across **Stop → Continue** (see D14).

**Tests.** Three outgoing connections → departures cycle 0,1,2,0. Two Starters passing through the same Station interleave on one shared counter. Play resets; Continue does not.

---

## D7 — Probability failure

[x] Accepted

**Decision (P2).** A failed probability check makes the Station **silent for that arrival only**. Everything else proceeds exactly as if it had fired:
- the note is still resolved and becomes the carried note context (see D19);
- repeats still occupy their time, silently;
- the Traveler departs at the same tick to the same branching choice.

A muted Station (manual or gate) behaves identically. Probability is evaluated **once per arrival**: a Station with repeats either plays the whole repeat group or none of it.

Probability 0 = never sounds; 100 = always sounds, and no random draw is consumed at 100 or 0 (so endpoints are exact and don't disturb random streams).

**Why.** If probability stopped the chain, a 90% Station in a loop would eventually kill the loop with certainty. If it rerouted, probability would become a hidden branching mode. Silent pass-through keeps probability a purely sonic control.

**Tests.** 0% and 100% endpoints exact with no RNG draw; with a fixed seed a 50% Station produces an identical fire/skip sequence across runs and platforms; a skipped Station's downstream timing is identical to a fired one.

---

## D8 — Repeat count meaning and timing

[x] Accepted

**Decision.** The parameter is `additionalRepeats` (N ≥ 0): **N extra triggers after the first**. N = 0 means play once. The UI must label the total so nobody guesses, e.g. "Repeats: 2 (plays 3×)".

Timing: a new per-Station `repeatIntervalTicks` field, defaulting to the Station's note duration.

```
arrival at T:
  triggers at   T, T + I, T + 2I, ... T + N*I
  each trigger  is its own note with its own Note Off at trigger + duration
  departure at  T + N*I       (with N = 0, departs at T, same as no repeat)
```

Stop or Panic during a repeat group discards the pending repeats; notes already sounding are released per D14.

**Tests.** N = 2 emits exactly 3 Note On/Off pairs; departure at T + 2I; Stop between repeats yields no further Note On and no stuck note.

---

## D9 — Relative Value with no previous note

[x] Accepted

**Decision.** When no note context exists (the first Relative Station in a chain), the reference is the **Document Root in octave 4**: `60 + rootPitchClass` (C → 60, A → 69). An interval of 0 resolves to that root.

Step counting from a reference:
- `+k` = the k-th in-scale note strictly above the reference;
- `−k` = the k-th in-scale note strictly below the reference;
- `0` = the reference if it is in scale, else the nearest in-scale note (ties resolve downward, as in D11).

This defines behavior when the reference is out of scale, which happens after an absolute Station that isn't in the Document Scale. Relative-mode output is therefore always in scale.

**Tests.** C major, ref 60: +1 → 62, −1 → 59, +7 → 72. Ref 61 (out of scale): +1 → 62, −1 → 60, 0 → 60.

---

## D10 — MIDI range overflow and Random Value Mode

[x] Accepted

**Range rule.** If a resolved note falls outside 0–127, **fold by octaves** into range (−12 until ≤ 127, +12 until ≥ 0). Octave folding preserves pitch class, so the note stays in scale. Clamping would pin runaway melodies to a single, often out-of-scale, edge note.

**Random Value Mode.** The behavior notes this mode refers to are not in the pack, so this definition is authoritative. A Random Value Station has a sub-mode:

- **Anchor** (default): the Station stores an **anchor note** (absolute MIDI) and a **range R** in scale steps. On each trigger, draw an integer `k` uniformly in `[−R, +R]` from the Station's value stream and resolve `anchor + k` scale steps using the D9 step rules.
- **Walk**: a bounded random walk ("drunk walk") around the anchor. The reference is the incoming carried note (D9 default if none). Draw `k` uniformly in `[−R, +R]`, step `k` scale degrees from the reference, then, if the result is more than `W` scale steps from the anchor, **reflect** it back inside the bound (mirror at the boundary). Parameters: anchor, step size R, bound W. Melodies wander but always stay within ±W of the anchor, which gives evolving lines that never drift off.

Both sub-modes use the same draw, the same stream, and the same range folding. The result becomes the carried note context like any other resolved note.

**Seed scope** (applies to all randomness: probability, Random branching, Random Value Mode):
- The project stores `randomSeed` and `seedMode` (`fixed` | `newEachPlay`, default `newEachPlay`). The seed actually used is shown in the UI and can be locked with one click.
- Each Station gets **independent streams** derived from `(seed, StationId, purpose)`, where purpose is probability, branch, or value, plus a per-stream draw counter. Use a counter-based generator (e.g. SplitMix64 or PCG). Adding or editing one Station never changes another Station's random outcomes.
- **Do not use `std::uniform_int_distribution` or other `<random>` distributions.** Their output differs between MSVC, libstdc++, and libc++, which would break cross-platform determinism. Implement bounded draws directly (e.g. Lemire's method) and cover them with golden-value tests.

**Tests.** 120 + 12-step overflow folds into range and stays in scale; golden sequences for a fixed seed identical on all three CI platforms; adding an unrelated random Station leaves an existing Station's sequence unchanged.

---

## D11 — Force Scale tie-breaking and boundaries

[x] Accepted

**Decision.**
- In-scale notes are preserved unchanged.
- Out-of-scale notes snap to the nearest in-scale pitch. **Ties resolve downward** (C# in C major → C). Downward rounding is context-free and therefore deterministic.
- If the nearest candidate is outside 0–127, take the nearest candidate inside the range.
- Force Scale applies only to absolute-mode Stations, never as a second pass on Relative output (as the spec already states).

**Scale representation.** Store Document Scale as a **12-bit pitch-class mask** relative to Document Root, with named presets (Major, Natural/Harmonic/Melodic Minor, the modes, Major/Minor Pentatonic, Blues, Chromatic). Custom scales then cost nothing extra. Validation: the mask must contain at least one pitch class. With Chromatic, relative steps are semitones and Force Scale is a no-op.

**Tests.** Every pitch 0–127 against every preset: output in scale, in range, and idempotent (applying twice equals applying once).

---

## D12 — Logic gate model, cadence, and cycles

[x] Accepted

The pack did not define what a gate's inputs are, and that had to be settled before cycles could be. Model (decided by Claude at the owner's request):

**Inputs (boolean signals):**
- **Station Active:** true while any note from that Station is sounding (Note On to Note Off). Always false while stopped.
- **Gate output:** the output of another gate.
- **Switch:** a manual on/off toggle on the canvas.
- **MIDI In:** true while a mapped note is held, or while a mapped CC is ≥ a threshold (see D15).

**Gates:** AND, OR, XOR (odd parity for >2 inputs), NOT, NAND, NOR. **Output target:** one or more Stations' *enable* state. Each Station has at most one driving gate (validation error otherwise; compose with more gates if needed). A Station sounds only if it is manually unmuted **and** its driving gate (if any) outputs true.

**Cadence:**
- The logic graph is re-evaluated whenever any input changes, on the engine thread, at tick boundaries while playing and on each input event while stopped. This satisfies "evaluates continuously while stopped or playing" without a separate timer.
- **Arrivals at tick T see the mute state as of the end of tick T−1.** Station Active signals produced at T affect logic from T+1. This makes same-tick arrivals independent of processing order.

**Cycles:**
- Acyclic portions evaluate in topological order within one step (no lag).
- Each cycle is broken at a deterministic feedback edge, the first back-edge found by DFS from the lowest `GateId`, which reads the **previous step's** value (one step of delay, like a register).
- A NOT gate fed back into itself therefore toggles once per evaluation step, and is stable when no input changes.

**Tests.** Gate outputs change only mute state and never Traveler count or destinations; logic responds to a Switch while stopped; NOT feedback loop toggles deterministically per step; two Stations arriving on the same tick, one of which drives the other's gate, behave identically regardless of processing order.

---

## D13 — Terminators

[x] Accepted

**Decision.** A Terminator is a **per-Station property**: `terminatorLimit` (none, or an integer L ≥ 0).

- The Station counts **every Traveler arrival**, including silent ones (P2: Terminators count Travelers, not notes).
- Arrivals 1…L proceed normally. Arrival L+1 and later are **terminated**: no trigger, no onward travel, the Traveler is removed (P3).
- **L = 0 is "Block", not "mute".** Every arrival is terminated. The spec currently calls this "permanent mute"; rename it, because muting (P2) lets Travelers through and this does not.
- Scope: per Station, shared across all Starters.
- Reset: on **Play**, and by a manual reset control. Preserved across Stop → Continue.
- Save/load: the counter is runtime-only and **not saved**. Since loading never starts playback and Play resets counters, behavior after load is deterministic.

**Tests.** L = 2: arrivals 1–2 pass, 3+ terminate; L = 0 terminates all; a muted arrival still increments the count; save → load → Play starts at 0.

---

## D14 — Transport and MIDI Clock behavior

[x] Accepted

| Action | Engine | MIDI out |
|---|---|---|
| **Play** | Reset all runtime state (Travelers, round-robin counters, terminator counts, note context, seed per `seedMode`). Tick = 0. Spawn one Starter per Start Station; each triggers at tick 0 (Start Stations ordered by `StationId`). | Optional SPP = 0, then **Start**, then the first Clock at tick 0, then tick-0 notes. |
| **Stop** | Takes effect at the **next MIDI Clock pulse boundary** (every 40 ticks at 960 PPQ, ≤ 21 ms at 120 BPM). Freeze runtime state in place. | Note Off for every active note, then **Stop**. Clock pulses cease. |
| **Continue** | Resume frozen state from the stopped tick. Notes cut at Stop are not re-triggered. | **Continue**, then Clock resumes on the next pulse. No SPP. |
| **Panic** | Clear the active-note registry and pending repeats. Does not change transport state. | On all 16 channels: Note Off for tracked notes, CC 123 (All Notes Off), CC 120 (All Sound Off), Pitch Bend centered. |

- Quantizing Stop to a clock pulse means SignalFlux and any slaved device agree on the pulse count, so **Continue** realigns correctly without SPP.
- There is no timeline, so there is no reposition/SPP feature beyond optional SPP 0 before Start.
- Clock is sent only while running by default, with a preference to send it continuously while stopped (some hardware prefers that).
- v1 is **clock master only**. Syncing to external clock is deferred.
- Tempo changes apply at the next tick. Ticks are unaffected, and only the tick-to-time mapping changes.

**Tests.** 24 clocks per quarter at several tempos with no drift over 10 minutes; Stop → Continue preserves Traveler positions; no stuck notes after Stop mid-repeat.

---

## D15 — MIDI input messages and mapping

[x] Accepted

**Supported in v1:** Note On/Off, Control Change, Pitch Bend. **Ignored:** SysEx, Program Change, incoming Clock/transport, Aftertouch (deferred).

**Mapping model:** a project-level table of `source (device, channel or Any, message type, number) → target`. v1 targets:
- **Global transpose** in scale steps (applied after resolution, before range folding);
- **Tempo** (CC mapped linearly onto the tempo range);
- **Gate input** (note held / CC ≥ threshold; see D12);
- **Station velocity** or **Station probability** (CC mapped 0–127 → valid range).

Input is timestamped on arrival and applied as an engine command at the next tick boundary. MIDI thru is off and not in v1.

**Tests.** Mapped CC changes tempo while running; mapped note held toggles a gate while stopped; unmapped messages have no effect.

---

## D16 — Multiple MIDI outputs

[x] Accepted

**Decision.** v1 supports **one selected MIDI output device**. Every Station uses that device; its channel field selects the channel. Clock goes to the same device.

- On macOS and Linux, also offer a **virtual output port** named "SignalFlux", so DAWs can receive from it directly. Windows lacks a native virtual-port API for this, so document using a loopback driver such as loopMIDI there.
- The schema stores outputs as a list of routes (one entry in v1) so multi-output can be added without a migration.
- Device disconnect: mark the output unavailable, flush the active-note registry internally, keep the engine running, and offer reconnect when the device returns.

---

## D17 — Project file format

[x] Accepted

- Extension **`.sflux`** (check for collisions with existing registered types before locking).
- **UTF-8 JSON**, top-level `"format": "signalflux-project"` and integer `"schemaVersion"`. JSON keeps fixtures human-readable and diffable in tests.
- **Atomic save:** write to a temp file in the same directory, flush, then rename.
- **Autosave** every 60 s while dirty, and before project replacement, to a recovery file in the user app-data directory. On launch, if a recovery file is newer than the saved project, offer to restore it. **Restoring never starts playback.**
- Unknown future `schemaVersion` → refuse with a clear message, no mutation. Older versions → run the migration chain, covered by fixtures.
- Device identifiers are saved by name, plus platform identifier where available. A missing device loads with the output marked unavailable.

---

## D18 — Defaults

[x] Accepted

| Setting | Default | Notes |
|---|---|---|
| Tempo | 120 BPM | Range 20–300 |
| Internal resolution | **960 PPQ** | Divisible by 2⁶ and 3, so straight and triplet values are integer (1/8T = 320, 1/16T = 160) |
| Grid spacing | 1 quarter = 960 ticks | Options: 1/1, 1/2, 1/4, 1/8, 1/16, 1/32 and triplets of each |
| Clock start position | Tick 0 on Play | |
| Note duration | 480 ticks (an eighth) | Stored in ticks; unaffected by grid-spacing changes |
| Velocity | 100 | Valid 1–127; 0 is not a valid Station velocity |
| Channel | 1 | |
| Probability | 100 | |
| additionalRepeats | 0 | |
| Plot size | 64 × 64 cells | |
| Max concurrent Travelers | **8** | Owner decision; configurable 1–256. See D23 |
| Max active notes | 256 | See D23 |

---

# Additional decisions found while reviewing the pack

These weren't on the original list but block deterministic implementation.

## D19 — "Previous played note" means previous *resolved* note

[x] Accepted

`BEHAVIORAL_SEMANTICS §4` says relative intervals resolve from "the previous Station's played note". Under P2, a muted or probability-skipped Station does not *play*. **Decision:** the carried note context is the previous **resolved** note, whether or not it sounded.

**Why.** Otherwise muting one Station via a gate silently transposes every Relative Station downstream, which would make logic gates alter pitch, not just mute. With this rule, the melodic contour is stable and silence only removes notes.

The carried context travels with the Traveler as explicit runtime signal data (as the spec requires), and is copied to every child on Instant branching.

**Spec fix required.** Change "played note" to "resolved note" in §3 and §4.

## D20 — Note duration vs. arrival

[x] Accepted

`BEHAVIORAL_SEMANTICS §1 Arrival step 1` ("end the current note with Note Off") implies notes last until the next arrival, while §3 says each Station has its own duration. These conflict.

**Decision.** Each note's Note Off is scheduled **at Note On** using the Station's own `durationTicks`, independent of travel. Arrival step 1 is removed. A "hold until next arrival" (legato) mode is deferred to v1.1. With Instant branching or a dead-end Station it has no single obvious meaning and needs its own decision.

**Spec fix required.** Delete Arrival step 1 and renumber.

## D21 — Event order within a tick

[x] Accepted

At each tick T, the engine processes in this fixed order:
1. Apply queued commands (edits, MIDI-input mappings), recomputing leg deadlines per D3/D4.
2. Note Offs due at T.
3. Arrivals and repeat triggers due at T, ordered by `TravelerId` ascending. Mute state is read from the T−1 snapshot (D12). Resolve → probability → Note On → schedule Note Off → depart.
4. Logic re-evaluation with signals produced at T (visible from T+1).

`TravelerId`s come from a counter reset on Play. Children created by Instant branching receive IDs in outgoing-order.

Because Note Off precedes Note On, a note ending exactly when the same pitch starts never cuts the new note.

## D22 — Overlapping notes with the same channel and pitch

[x] Accepted

**Decision: reference counting per (channel, pitch).** Every accepted Note On is sent, so overlaps retrigger. A Note Off is sent only when the **last** overlapping instance ends. The channel and pitch are captured at Note On, so changing a Station's channel mid-note releases on the original channel. The internal synth uses the same rule.

**Test.** Note A (60, ch1, T0–T960) and note B (60, ch1, T480–T1440): two Note Ons at 0 and 480, one Note Off at 1440.

## D23 — Overload behavior

[x] Accepted

Instant branching inside a loop multiplies Travelers on every cycle, so caps are required, not optional.

- **Traveler cap** (default **8**, project setting, range 1–256): when a departure would exceed the cap, the excess children are not spawned, starting from the end of outgoing-order. Each drop is counted and shown as a UI warning. Starters count toward the cap: with more Start Stations than the cap, the lowest `StationId`s spawn first.
- **Capacity vs. default.** The confirmed baseline requires *support* for at least 16 concurrent Travelers. The engine is built and stress-tested at 16, 64 and 256 (per the test plan); 8 is the shipped default that keeps patches musically readable. Tests must cover the cap at both 8 and 256.
- **Active-note cap** (default 256): a Note On beyond the cap is dropped (and so has no Note Off to schedule), counted, and shown in the UI.
- **Output queue overflow:** Note Off and Panic take priority over CC and Pitch Bend, as the spec already requires.

## D24 — End of chain

[x] Accepted

A Station with no outgoing connections triggers normally, and then its Traveler ends. If every Traveler has ended, the transport keeps running (clock continues) until the user stops it, and the UI shows "No active Travelers". Auto-stop is deferred.

## D25 — Cross-platform determinism (owner priority: "key")

[x] Accepted

**Requirement.** Given the same project, seed, and command stream, the engine produces a **byte-identical event log** on Windows, macOS, and Linux, in Debug and Release. This is a release gate, not a goal.

Rules for every module that affects musical output:
- **No floating point in engine decisions.** Ticks, cells, durations, and note math are integers (`int64_t` for ticks). Tempo is stored as integer **milli-BPM** (120 BPM = 120000).
- **Tick → time conversion happens only at the output boundary**, in 64-bit integer arithmetic with a carried remainder (`samples = ticks × sampleRate × 60000 / (milliBpm × PPQ)`), so there is no drift and no float rounding differences between platforms.
- **Randomness** only via the D10 counter-based streams. No `<random>` distributions, no `rand()`, no time-based seeds inside the engine.
- **Ordering.** Nothing that affects output may iterate `unordered_map`/`unordered_set`, sort by pointer address, or depend on thread scheduling. Use explicit IDs and the D21 order.
- **Compiler settings.** No `-ffast-math`, `/fp:fast`, or equivalent anywhere in the engine target. Warnings-as-errors on all three compilers.
- **Serialization is canonical.** Stable key order, integers for all musical fields, locale-independent number formatting (`std::to_chars`), UTF-8, LF line endings. Saving the same document produces identical bytes on every platform.

**Enforcement.**
- A **headless renderer** (`signalflux-render`, a console target that links the engine only, with no GUI, audio, or MIDI devices) runs a project for N ticks with a given seed and writes the canonical event log.
- **Golden tests** in CI run every fixture project through the renderer on all three platforms and compare against committed golden logs byte for byte. Any cross-platform difference fails the build.
- The same renderer is the basis for stress tests (D23) and for the Capture/MIDI-file export feature, if adopted.

## D26 — Station event types (gap in the pack)

[x] Accepted

The spec requires outgoing CC, CC ramps, and Pitch Bend, but nothing defines what produces them. **Decision:** each Station has an **event type**:

| Type | Emits | Parameters |
|---|---|---|
| **Note** (default) | Note On/Off | note mode (absolute / relative / random value), velocity, duration, channel |
| **CC** | Control Change | CC number, target value; optional **ramp**: glide from the channel's current value to the target over `durationTicks` |
| **Pitch Bend** | Pitch Bend | target value (−8192…+8191); optional ramp as for CC |

- All types share routing, branching, probability, mute, repeat, and Terminator behavior (P2 and P3 apply unchanged).
- CC and Pitch Bend Stations **do not change the carried note context**, so you can drop a filter sweep into a melodic chain without disturbing Relative Values.
- Ramps emit at most once every 10 ticks (96 steps per quarter note), and only when the integer value changes.
- A newer ramp on the same (channel, controller) replaces the older one (last writer wins). Stop freezes ramps where they are. Panic recenters Pitch Bend.
- CC and Pitch Bend messages are low-priority under output overflow (D23).

---

# Beta feature scope (owner-approved additions, 2026-11-23)

These features are added to the beta. Each one obeys P1–P4 and D25: none of them may introduce floating-point timing, unordered iteration, or unseeded randomness into the engine.

## D27 — Undo / redo

[x] Accepted

- Every document edit is a command applied to the document model. The undo stack stores inverse commands, with no limit beyond memory (default cap 500 steps).
- Continuous gestures (dragging a Station, turning a value) coalesce into **one** undo step per gesture.
- **Undoable:** structural and parameter edits (Stations, routes, gates, scale/root, mappings, scenes' contents, project settings).
- **Not undoable (performance actions):** transport, seed changes, scene recall, live Switch toggles, MIDI-input modulation, Capture. These would make undo fight the performer.
- Undo during playback is an ordinary edit, applied at the next tick boundary under D3/D4/D5.

## D28 — Capture to MIDI file

[x] Accepted

- The engine writes every emitted MIDI event (with its tick) into a **preallocated ring buffer** holding the last **10 minutes** at the current tempo (configurable). It is written from the engine thread without allocation, and read on the message thread.
- **Capture** exports the buffer as a Standard MIDI File (type 1, 960 PPQ, one track per MIDI channel used, tempo map included) on the message thread. Disk I/O never touches the real-time path.
- **Render** (offline): the headless renderer (D25) exports N bars from a given project and seed directly to `.mid`. Because the engine is deterministic, Render with the seed of a live run reproduces it exactly, provided no live edits or MIDI input occurred.
- Captured files carry the seed and project name in a text meta-event.

## D29 — Seed history

[x] Accepted

- Every Play records the seed used. The app keeps the **last 32** seeds (app preferences), each with time and project name.
- Seeds can be **starred**. Starred seeds are saved in the project file.
- One click recalls a seed and switches `seedMode` to `fixed`, so the next Play reproduces that run.

## D30 — MIDI Learn

[x] Accepted

- In Learn mode, clicking any mappable target (D15) and then moving a control or pressing a key creates the mapping. Esc cancels.
- A source already mapped elsewhere is moved to the new target, with a toast saying so (no silent duplicates).
- Learned mappings are ordinary undoable edits (D27) and are saved in the project.

## D31 — Beat ticks on routes

[x] Accepted

- Each route shows a small tick mark at every **beat** boundary along its length (not every cell), plus a stronger mark every 4 beats.
- With grid spacing of one quarter note and no per-route speed, that is one mark per cell. With a different grid spacing or a per-route speed (D33), the marks still show real beats, so route length in time is always readable.
- Ticks are drawn from the same integer leg-duration function the engine uses, so they cannot disagree with the timing.

## D32 — Weighted Random branching

[x] Accepted

- Each route has an integer `weight`, 0–100, default 1. Random branching chooses a route with probability `weight / sum(weights)`, using the Station's branch stream and an integer bounded draw (no floats).
- Weight 0 = never chosen by Random. If all weights are 0, the Traveler ends there (consistent with "0 = never"), and the UI marks the Station with a warning.
- Sequential and Instant branching ignore weights.

## D33 — Per-route speed

[x] Accepted

- Each route has a **rate** `num/den`, default 1/1. Leg duration becomes:
  `leg_duration_ticks = length_cells × grid_spacing_ticks × den / num`
- To keep durations exact integers (P4), rates are chosen from a fixed menu: numerator and denominator each from {1, 2, 3, 4, 5, 6, 8}, displayed as "×3/2" etc. The engine validates that the product divides exactly at the current grid spacing; if a grid-spacing change makes it inexact, the UI rejects the change with a message rather than rounding.
- Example: a 3-cell route at ×3/2 takes 2 beats. Combined with a 2-cell route at ×1, that gives 3-against-2 polyrhythm on the grid.
- In-flight legs follow D3/D4: elapsed ticks are preserved and the deadline is recomputed if the rate changes mid-flight.
- Sprites on faster routes visibly move faster; beat ticks (D31) show the true timing.

## D34 — Chord Stations

[x] Accepted

- A Note Station may have a **chord**: a list of up to 6 scale-degree offsets applied to its resolved note, e.g. `[0, 2, 4]` = triad, `[0, 2, 4, 6]` = seventh chord. Presets plus custom lists.
- Each chord tone is a separate note through the active-note registry (D22) and counts toward the active-note cap (D23). Chord tones are emitted in ascending pitch order (D21 determinism).
- Range folding (D10) is applied per chord tone.
- The **carried note context is the chord root** (offset 0), so Relative Stations downstream follow the root, not the top note.

## D35 — Scenes

[x] Accepted

- A Scene is a stored snapshot of: manual mute states, Switch states (D12), and tempo. Up to 8 scenes per project.
- Recall is quantized to the next **beat** by default (options: immediate, next beat, next 4 beats). Quantization uses ticks, so it is deterministic.
- Recall is a performance action (not undoable, D27). Scene slots are MIDI-learnable (D30), so a pad controller can switch scenes live.
- Editing what a scene contains is an undoable edit.

## D36 — Swing and humanize

[x] Accepted

- **Swing** (project-level, 50–75%, default 50% = straight; grid: 8ths or 16ths): shifts the **emission time** of events whose tick falls on an off-beat of the swing grid. Swing never changes leg durations or Traveler positions (P1). Integer tick arithmetic, rounding toward zero.
- **Humanize** (per Station, off by default):
  - velocity: ± up to N (clamped to 1–127);
  - timing: a **delay only** of 0 to N ticks (never early, so events can't be scheduled into the past).
  - Draws come from a dedicated `humanize` stream per Station (D10).
- Both affect only when and how loud notes sound. Logic, routing, and terminator counts are unchanged.

## D37 — Ableton Link (optional build component)

[x] Accepted — enabled (owner chose open source, 2026-11-23)

- Link provides network tempo and phase sync with Live and other Link apps. When enabled, Link's session tempo becomes the tempo authority, and Play can be quantized to the Link phase (next bar).
- **Licence:** Link is published under GPLv2-or-later, with a separate proprietary licence available from Ableton on request. It is built behind the CMake option **`SIGNALFLUX_WITH_LINK`, default ON**. Because SignalFlux is released under AGPLv3 (see Licensing), Link's GPLv2-or-later terms are satisfied and beta builds may include it.
- Link code is isolated behind the clock interface, so builds without it are identical in behavior apart from sync.

## D38 — Example projects

[x] Accepted

Six example projects ship with the beta, each also used as a golden-test fixture (D25):

1. **First Loop:** two Stations, one route, the simplest possible patch.
2. **Branches:** Sequential vs Random vs Instant side by side.
3. **Gatekeeper:** logic gates and a Switch muting parts of a loop.
4. **Wanderer:** Walk mode (D10) over a pentatonic scale.
5. **Polyrhythm:** per-route speeds (D33) and chord Stations (D34).
6. **Machine Room:** CC ramps, Pitch Bend Stations, Terminators, and Scenes.

---

## Licensing (not legal advice)

**Owner decision (2026-11-23): SignalFlux is open source under the GNU AGPLv3.** This satisfies JUCE's open-source licence (AGPLv3) and Ableton Link's (GPLv2-or-later, usable under GPLv3, which is compatible with AGPLv3). Every distributed build must be accompanied by, or offer, its complete corresponding source.

| Component | Licence | What it means for SignalFlux |
|---|---|---|
| **JUCE 9** (pinned 9.0.6) | AGPLv3, or JUCE's commercial EULA (free Starter tier up to $20k annual revenue/funding; paid tiers above) | Closed-source beta: use the Starter tier while under the limit. Open-source: AGPLv3. |
| **VST3 SDK** (only if a plugin is built later) | MIT since SDK 3.8 | No licence barrier to a future plugin. |
| **Ableton Link** | GPLv2-or-later, or a proprietary licence from Ableton | Closed-source: request a proprietary licence from Ableton. Open-source: compatible with releasing SignalFlux under (A)GPLv3. |

Giving beta builds to testers counts as distribution, so the source repository must be public (or provided to every tester) when builds go out.

---

## Spec edits required

| File | Section | Change |
|---|---|---|
| `README.md`, `PRODUCT_AND_SYSTEM_SPEC.md` | §1 | "place Plots on a grid" → "place Stations on a Plot's grid" (D1) |
| `BEHAVIORAL_SEMANTICS.md` | §1 Arrival | Remove step 1; renumber (D20) |
| `BEHAVIORAL_SEMANTICS.md` | §1 Travel | Replace "spatial_grid_distance" with the Manhattan route length; add the D3/D4 recompute rule |
| `BEHAVIORAL_SEMANTICS.md` | §3, §4 | "played note" → "resolved note" (D19) |
| `BEHAVIORAL_SEMANTICS.md` | §6 | Replace the baseline interpretation with D7 |
| `BEHAVIORAL_SEMANTICS.md` | §9 | "A limit of zero means permanent mute" → "permanent block" (D13) |
| `ARCHITECTURE_AND_INTERFACES.md` | §4 | Add `ScaleMask`, `RandomStreamKey`, `OutgoingOrder`, `StationEventType`, `MilliBpm` types |
| All documents and task briefs | Throughout | Point → Station (see Terminology) |
| `DECISIONS_AND_OPEN_QUESTIONS.md` | Confirmed baseline | "At least 16 concurrent Travelers" → "Engine supports at least 16 (tested to 256); default cap 8" (D23) |
| `tests/ACCEPTANCE_AND_TEST_PLAN.md` | §1 | Add cross-platform golden event-log comparison (D25) |
| `tasks/SF-001` | Acceptance | Add `signalflux-render` headless target (D25) |

## Owner decision log (2026-11-23)

| Item | Owner decision |
|---|---|
| Node terminology | Renamed Point → **Station** (Claude's choice, delegated) |
| D2 distance | Manhattan + orthogonal routes |
| D7 probability failure | Silent pass-through |
| D10 Random Value Mode | Interpretation accepted; Claude added the Walk sub-mode |
| D12 gate input model | Delegated to Claude; proposed model adopted |
| D13 Terminator limit 0 | Renamed **Block** |
| D19 note context | Includes silent Stations |
| D20 note duration | Fixed per-Station duration; legato deferred |
| D23 Traveler cap | Default **8** |
| D25 determinism | Cross-platform consistency is a release gate |
| D27–D38 | Beta features approved: undo/redo, Capture, seed history, MIDI Learn, beat ticks, weighted Random, per-route speed, chord Stations, Scenes, swing/humanize, Ableton Link (build flag), example projects |
| Licence | SignalFlux is AGPLv3 open source; Link enabled |
| Beta platforms | macOS and Windows. Linux app build deferred; engine tests still run on Linux in CI as a third-compiler determinism check |
| Plugin / VST3 | Remains out of scope for the beta; engine stays host-agnostic so a plugin phase is possible later |
