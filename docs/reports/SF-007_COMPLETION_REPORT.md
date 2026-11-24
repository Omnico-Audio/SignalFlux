# Agent Completion Report

## Task
SF-007: MIDI Services.

## Summary
Device-independent MIDI core (`src/midi`): wire encoding, an exact integer tick/time clock with 128-bit intermediates, MIDI Clock pulse scheduling, a lock-free SPSC queue, and an input router with mappings and MIDI Learn. The engine gained CC and Pitch Bend Stations with ramps (D26), global transpose and per-Station velocity/probability overrides (D15). `src/app/EngineHost` runs the Sequencer on a 1 ms high-resolution timer thread, sends MIDI, receives input, handles device loss, Scene-quantized document swaps and Ableton Link tempo sync. The main window is an interim test harness (example + output picker, Play/Stop/Continue/Panic) until SF-009.

## Acceptance status
| Criterion | Status / evidence |
|---|---|
| All 16 channels | `Note On and Note Off on all 16 channels`; engine channels 1–16 throughout |
| MIDI Clock at 24 PPQ | `MIDI Clock: 24 pulses per quarter at every tempo`; host interleaves pulses in tick order |
| Stop / Continue / Start documented | D14; encoding tests (SPP 0 + Start, Stop, Continue); host order SPP → Start → Clock → tick-0 notes |
| MIDI input modulation while running | Router tests (tempo, probability, scene edge, learn); engine tests for transpose and overrides while playing |
| Panic silences all channels | `Panic silences all 16 channels`: CC 123 + CC 120 + centred Pitch Bend × 16, after Note Offs for every tracked note |
| Device disconnect handled safely | Host: device-list watcher → `OutputLost` → output dropped, internal Panic, engine keeps running (compile-verified; needs hardware check) |
| Synth is not required for MIDI output | Output path is EngineHost → MIDI device; no synth dependency |

## Files changed
- New: `src/midi/{MidiBytes,PlaybackClock,MidiInputRouter}.{h,cpp}`, `src/midi/SpscQueue.h`, `src/app/EngineHost.{h,cpp}`, `tests/unit/MidiTests.cpp`, this report.
- Modified: `src/engine/Sequencer.{h,cpp}` (CC/PB ramps, transpose, overrides, `snapshotInto`), `src/engine/EngineEvent.{h,cpp}` (CC, PB), `src/app/MainComponent.{h,cpp}` (test harness), CMake lists.

## Decisions made
- MIDI is sent from the engine thread with `sendMessageNow` on a 1 ms timer: worst-case lateness ≈1 ms, no lookahead in the beta. A timestamped lookahead scheduler can replace it without engine changes.
- Ramps: at most 64 concurrent; on overflow the value jumps straight to the target. Ramp steps run after Note Offs and before arrivals within a tick.
- Transpose applies to emitted notes only; the carried melodic context is unchanged.
- Keyboard-note transpose mappings use note 60 as "no shift"; scene recall fires on rising edges.
- Link: tempo sync only (both directions). Phase-aligned Start is not in the beta.
- Windows has no virtual port; the harness offers "No output" there and any loopback driver appears as a normal device.

## Tests run
| Command | Result |
|---|---|
| GCC 13.3 Debug / GCC Release / Clang 18.1 Release, `-Werror`, `ctest` | 189/189 passed in each |
| App sources (`EngineHost`, `MainComponent`, `Main`) compiled against JUCE 9.0.6 + Link 4.1.1 on Linux | Compiled; full app link runs in CI on macOS and Windows |

## Known limitations (need hands-on testing)
- Real devices, virtual ports, disconnect/reconnect, Link sessions and timing jitter have not been exercised; record results per ACCEPTANCE §7.
- MIDI input device names are matched as strings on the engine thread (small allocation per message).

## Real-time / thread-safety review
- Threads: message (UI), engine (HighResolutionTimer), JUCE MIDI input.
- Engine thread: no blocking locks (snapshot publish and Learn hand-off use `try_lock` and retry); commands, input and output events through bounded SPSC queues. `setDocument` compiles the graph on the engine thread (allocates, not the audio callback).
- Queue bounds: commands 256 (UI call returns false), input 1024 (drop + count), events 8192 (drop + count).
