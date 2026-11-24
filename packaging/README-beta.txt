SignalFlux beta
===============

SignalFlux is a grid-based, non-linear MIDI sequencer. This is a beta build:
please report anything odd, with the project file if you can.

Installing
----------
macOS (Apple Silicon): drag SignalFlux into Applications. This beta is not
notarized by Apple, so the first time, right-click SignalFlux and choose Open,
then confirm. (Or: System Settings > Privacy & Security > Open Anyway.)

Windows (64-bit): unzip anywhere and run SignalFlux.exe. Windows SmartScreen may
warn because the beta is not code-signed: choose "More info" > "Run anyway".

Getting sound out
-----------------
- To hear it right away, tick "Synth" in the toolbar (needs an audio output).
- To play other instruments, choose a MIDI output in the toolbar.
  macOS: pick "SignalFlux virtual port" and select it as a MIDI input in your
  DAW (Ableton Live: Settings > Link, Tempo & MIDI > Input "SignalFlux": Track on).
  Windows: install a loopback driver such as loopMIDI, create a port, choose it
  in SignalFlux and in your DAW.
- Tempo sync with Live: turn on "Ableton Link" in the Project inspector (click
  an empty part of the grid to see project settings), or send MIDI Clock.

Quick start
-----------
1. Station tool: click cells to place Stations. The first one is the Start.
2. Select tool: Shift-drag from one Station to another to connect them.
3. Press Space to play, Esc to silence everything.
4. Select a Station to edit its note, chord, probability, repeats and more.
5. Open the files in "Examples" to see what SignalFlux can do.

Source code and licence
-----------------------
SignalFlux is free software under the GNU Affero General Public License v3
(see LICENSE.txt). The complete source code for this build is available at
https://github.com/Omnico-Audio/SignalFlux at the matching release tag.
It uses JUCE (AGPLv3) and Ableton Link (GPLv2 or later).
