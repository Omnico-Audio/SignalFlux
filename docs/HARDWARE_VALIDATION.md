# Hardware / Virtual MIDI Validation Record (ACCEPTANCE §7, SF-010)

Fill in one row per device and platform tested. "Stuck notes" means any note
still sounding after Stop, Panic, device unplug or quitting the app.

| Date | Tester | OS + version | SignalFlux build | Device / port and driver | Connection | Destination (DAW or hardware) | Clock stable? | Latency / jitter notes | Stuck notes? | Disconnect handled? | Known limitations |
|---|---|---|---|---|---|---|---|---|---|---|---|
| | | | | | | | | | | | |

## Suggested test set

- macOS: SignalFlux virtual port → Ableton Live; IAC Driver → Logic Pro; one USB MIDI interface → hardware synth.
- Windows: loopMIDI → Ableton Live; one USB MIDI interface → hardware synth.
- Both: Ableton Link session with Live; MIDI Clock slave (e.g. a drum machine) following Start/Stop/Continue.

## Status

No hardware validation has been recorded yet. The engine, MIDI encoding, clock maths and file format are covered by automated tests on macOS, Windows and Linux; device behaviour can only be confirmed here.
