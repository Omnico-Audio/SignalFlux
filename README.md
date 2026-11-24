# SignalFlux — Greenfield Developer Pack v2.0

**Status:** Initial engineering baseline  
**Product:** SignalFlux  
**Implementation:** C++ / JUCE / CMake  
**Application type:** Standalone desktop application  
**Platforms:** Windows, macOS, Linux

This pack defines a clean-room implementation. It does not require, depend on, or port any existing application source code. The product behavior in these documents is the specification baseline. Where behavior is ambiguous, create a documented decision and a reproducible test rather than guessing.

## Start here

1. Read `AGENTS.md`.
2. Read `docs/PRODUCT_AND_SYSTEM_SPEC.md`.
3. Read `docs/BEHAVIORAL_SEMANTICS.md`.
4. Read `docs/ARCHITECTURE_AND_INTERFACES.md`.
5. Execute tasks in `tasks/` in dependency order.
6. Treat `tests/ACCEPTANCE_AND_TEST_PLAN.md` as release criteria.

## Core product statement

SignalFlux is a grid-based, non-linear MIDI sequencer. Stations are placed on a Plot's spatial grid and connected with Flowpoints to create looping, branching, and randomized musical structures. A logic-gate system controls Station muting, while Nomadic sprites visualize signal movement. SignalFlux sends MIDI to DAWs and hardware and can optionally generate sound using an internal 16-voice synthesizer.

SignalFlux is not a DAW and is not a plugin. It is a standalone MIDI instrument/controller application.

## Building

Requirements: CMake ≥ 3.24, a C++20 compiler, Git. Dependencies (JUCE 9.0.6, Catch2 3.16.1, Ableton Link 4.1.1) are fetched automatically at pinned commits; see `cmake/SignalFluxDependencies.cmake`.

| Platform | Configure | Build | Test |
|---|---|---|---|
| macOS (Xcode) | `cmake --preset macos` | `cmake --build --preset macos-release` | `ctest --preset macos-release` |
| Windows (VS 2022) | `cmake --preset windows` | `cmake --build --preset windows-release` | `ctest --preset windows-release` |
| Engine only, any OS (Ninja, no JUCE) | `cmake --preset engine` | `cmake --build --preset engine` | `ctest --preset engine` |

The app bundle / executable lands in `build/<preset>/src/app/SignalFlux_artefacts/Release/`.

Targets:
- `SignalFlux` — the desktop application (`juce_add_gui_app`).
- `signalflux_core` — domain + engine, pure C++20, no JUCE.
- `signalflux_tests` — unit tests (Catch2), core only.
- `signalflux-render` — headless engine runner used for determinism tests and offline rendering.

Options: `-DSIGNALFLUX_WITH_LINK=OFF` builds without Ableton Link.

Beta platforms are macOS and Windows. The Linux app build is deferred; CI still builds and tests the engine on Linux with GCC as a cross-platform determinism check.

## Licence

SignalFlux is free software under the GNU Affero General Public License v3 (`LICENSE`). It uses JUCE under the AGPLv3 and Ableton Link under the GPLv2-or-later.

## Non-negotiable engineering principles

- The spatial grid is not a timeline.
- Musical scheduling is computed by the engine, never by sprite animation.
- Visual Travelers are projections of engine state.
- MIDI output must function with the internal synth disabled.
- Logic evaluation is a distinct subsystem and does not redirect Travelers.
- MIDI output and the optional synth consume the same generated musical events.
- The audio/MIDI real-time path must avoid blocking, unbounded work, and unpredictable allocation.
- Each project must be self-contained and versioned.
- All supported operating systems must be considered in architecture and tests from the beginning.
