# Acceptance and Test Plan

## 1. Build matrix

CI must build and run tests on:
- Windows x64
- macOS (Apple Silicon; add Intel only if release support is intended)
- Linux x64

Record compiler, JUCE revision, CMake version, and build configuration.

## 2. Core behavior tests

- Each Start Station creates one Starter on Play.
- Starter emits immediately at start.
- Path distance multiplied by grid spacing determines leg duration.
- Moving a destination Station during flight updates the remaining arrival deadline according to the selected geometry policy.
- Traveler visuals do not affect event timing.
- Sequential branching round-robins.
- Random branching is seedable and reproducible.
- Instant branching triggers all selected destinations at the same scheduling instant.
- Pass carries prior note data and overrides Relative Values.
- Relative Values count in-scale steps from the correct reference.
- Relative Pass-through forwards the incoming offset unchanged.
- Force Scale preserves in-scale absolute notes and snaps out-of-scale notes.
- Probability endpoints 0% and 100% behave exactly.
- Repeat emits the defined number of repeated note events.
- Logic gates only alter Station mute state and do not redirect Travelers.
- Logic evaluation continues while playback is stopped.
- Terminator count and reset behavior match the approved rule.

## 3. MIDI tests

- Note On / Note Off on all 16 channels.
- CC immediate values and ramps.
- Pitch Bend range and serialization.
- MIDI Clock is 24 pulses per quarter note.
- Start / Continue / Stop / Song Position behavior.
- MIDI input drives configured modulation while running.
- Device disconnect does not crash the application.
- Panic clears active notes on all channels.
- Same pitch/channel overlapping-note release semantics are deterministic.
- Station channel change does not interrupt a note already sounding.
- External MIDI output works with synth disabled.

## 4. Audio tests

- 16 simultaneous synth voices.
- Deterministic voice stealing.
- Audio device unavailable: MIDI remains operational.
- Sample rate and buffer size changes do not change musical event positions.
- No allocations or blocking locks in the audio callback.

## 5. Project tests

- Save / load round trip preserves all musical data.
- Unknown future schema version fails gracefully.
- Older schema migration is covered by fixtures.
- Missing MIDI device does not prevent project load.
- Autosave recovery does not auto-start playback.
- Corrupt project reports a useful error without partial destructive mutation.

## 6. Performance / stress

Define reproducible workloads, including:
- 16, 64, and 256 concurrent Travelers;
- dense branching;
- high repeat rates;
- CC ramps;
- MIDI input modulation;
- rapid grid edits during playback.

Measure CPU, memory, queue high-water marks, event lateness, and dropped events. Set release thresholds only after measurements on named reference hardware.

## 7. Manual hardware validation

Test representative devices and virtual MIDI ports on each platform. Record:
- device name and driver;
- OS version;
- connection type;
- MIDI clock stability;
- note latency / stuck-note behavior;
- DAW or hardware destination;
- known limitations.
