# SignalFlux Product and System Specification

**Version:** 2.0  
**Status:** Greenfield baseline

## 1. Product definition

SignalFlux is a grid-based, non-linear MIDI sequencer. Instead of a linear timeline, users place Stations on a Plot's grid and connect them with Flowpoints to create looping, branching, sequential, or randomized sequences. A logic-gate system adds control, and Nomadic sprites visualize signal flow in real time.

SignalFlux is a standalone desktop MIDI instrument/controller application. It is not a DAW and is not a VST or other plugin.

## 2. Supported platforms

- Windows
- macOS
- Linux

The application must be designed for cross-platform behavior from the beginning. Platform-specific MIDI device access is isolated behind a common interface.

## 3. Core concepts

- **Plot:** A project canvas / spatial workspace containing Stations and connections.
- **Station:** A musical node that can emit an event, pass note data, apply transformations, or control a chain.
- **Flowpoint / Path:** A directed connection along which a Traveler progresses.
- **Traveler / Nomadic sprite:** A visual representation of an active signal journey. It is not the scheduler.
- **Starter:** Runtime execution object created by a Start Station when playback begins.
- **Repeater:** Runtime behavior that re-triggers the current note before progression continues.
- **Logic gate:** Evaluates logical conditions and controls Station mute state.
- **Document Scale / Root:** Project-level musical scale configuration used by Relative Values and Force Scale.
- **Terminator:** Limits how many Travelers pass through a configured point or chain.

## 4. MIDI and sound

Required outgoing MIDI:
- Note On
- Note Off
- Pitch / note number
- Velocity
- Duration, represented by correct Note On/Off scheduling
- MIDI channel 1–16
- Control Change (CC)
- CC ramps / slew
- Pitch Bend
- MIDI Clock

MIDI output must work independently of the internal synthesizer.

The optional internal synthesizer:
- supports 16 simultaneous voices;
- is for monitoring / sound generation;
- must not gate, delay, or otherwise determine whether MIDI is sent externally.

MIDI input is required for input-driven modulation and must be routable into the engine while playback is active. Define supported MIDI input message classes and filtering in the MIDI subsystem specification.

## 5. Sequencing model

There is no fixed bar/beat arrangement timeline. The grid is spatial. Musical time is generated at runtime from:
- project tempo / internal clock configuration;
- grid spacing;
- path distance;
- Station behavior and active connections.

The first implementation must support a configurable grid spacing, with one quarter-note per grid cell as the default.

## 6. Playback and clock

The standalone application owns its playback clock. It must provide start, stop, continue, tempo control, and clock output. MIDI Clock must be emitted at the correct 24 pulses per quarter note. Start/Stop/Continue and Song Position Pointer behavior must be explicitly specified and tested for supported transport actions.

The sequencer's own event scheduler uses a high-resolution monotonic timebase / sample clock mapping. UI refresh is not a timing source.

## 7. Project lifecycle

- New, open, save, and save-as project workflows.
- Versioned project format with explicit schema version.
- Autosave / recovery strategy.
- Persist all musical content, grid geometry, Station settings, connections, logic configuration, scale/root, MIDI routing preferences, and relevant UI layout.
- Device identifiers may be unavailable on another machine; missing devices must not prevent project loading.
- Loading a project must not automatically start MIDI output unless explicitly chosen by the user.

## 8. Performance and robustness

- Minimum functional target: at least 16 concurrent Travelers.
- Note capacity should be bounded and configurable/tested; do not promise literally unlimited notes.
- Define deterministic overload behavior.
- Panic must silence all 16 MIDI channels and clear internal active-note state.
- Device disconnects, application shutdown, project changes, and playback stop must not leave stuck notes.
- CPU goals must be measured against a documented reference machine and workload. Do not make an unqualified percentage guarantee.

## 9. Out of scope for initial release

- DAW/plugin integration.
- Audio recording or mixing.
- Linear clip/arrangement timeline.
- Program Change / Bank Select unless later added by a product decision.
- Cloud collaboration or online accounts.
- Licensing / activation unless separately specified.
