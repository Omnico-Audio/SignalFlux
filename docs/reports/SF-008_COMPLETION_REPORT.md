# Agent Completion Report

## Task
SF-008: Optional 16-Voice Synth.

## Summary
`signalflux_audio` (JUCE-free): a 16-voice triangle synth with ADSR, deterministic voice stealing and allocation-free rendering. `app/SynthOutput` opens the default audio device and drains a bounded SPSC queue that `EngineHost` fills from the same engine event stream that drives MIDI output. The test window has a "Monitor synth" toggle; if no audio device opens it shows why and MIDI carries on.

## Acceptance status
| Criterion | Status / evidence |
|---|---|
| 16 voices | `16 simultaneous voices` |
| Deterministic voice stealing | Free → oldest releasing → oldest held, by Note On order; `Voice stealing...`, `Releasing voices are stolen before held ones`, `Voice stealing is deterministic` |
| Uses the same scheduled events as external MIDI | `EngineHost::onEngineEvent` encodes MIDI and feeds the synth from the same event |
| Disable synth and verify external MIDI output is unchanged | Synth is downstream only; `The synth consumes engine events without changing them`; disabling only stops rendering |
| Audio callback is bounded and allocation-free | Fixed voice array, fixed queue, no locks; `prepare()` (allocating) runs in `audioDeviceAboutToStart` |

## Files changed
- New: `src/audio/Synth.{h,cpp}`, `src/app/SynthOutput.{h,cpp}`, `tests/unit/SynthTests.cpp`, this report.
- Modified: `src/app/EngineHost.{h,cpp}` (synth queue), `src/app/MainComponent.{h,cpp}` (toggle), CMake lists.

## Decisions made
- Events apply at audio block start: monitoring jitter ≤ one buffer. Musical timing is unaffected because the engine decides it.
- A Note Off releases every voice with that (channel, pitch), matching D22.
- Stop and Panic send "all off" to the synth.

## Tests run
| Command | Result |
|---|---|
| GCC 13.3 Debug, `ctest` | 196/196 passed |
| App sources compiled against JUCE 9.0.6 | Compiled; full link runs in CI |

## Known limitations
- Sound and device behavior need checking on real hardware (ACCEPTANCE §4, §7).
