# SignalFlux AI Coding Agent Instructions

You are implementing SignalFlux from a clean slate using C++, JUCE, and CMake.

## Authority and scope

- The product and behavior specifications in `docs/` are authoritative.
- Do not introduce plugin targets, DAW-host transport dependencies, or a conventional linear arrangement timeline.
- Do not depend on an existing application source tree.
- Do not copy or port third-party source code. Implement behavior from the written specification.
- Use the product name SignalFlux in all new code, documentation, identifiers, and UI text.
- Do not add unrelated features without a recorded decision.

## Required workflow for every task

1. Read the task brief and all referenced specifications.
2. Identify unclear requirements and list them before implementation.
3. Propose the smallest coherent implementation and its public interfaces.
4. Implement with unit tests.
5. Run formatting, static analysis where configured, and relevant tests.
6. Report changed files, design decisions, test commands/results, limitations, and follow-up work.
7. Do not claim a test passed unless it was actually run.

## Architecture boundaries

- `domain`: project data and musical concepts; no JUCE UI dependencies.
- `engine`: deterministic sequencing, scheduling, logic, scale resolution, note lifecycle.
- `midi`: platform MIDI device adapters and protocol serialization.
- `audio`: optional internal synth and audio output.
- `ui`: grid, editors, visual Travelers, controls; observes engine state.
- `app`: startup, document lifecycle, dependency composition, platform integration.
- `persistence`: versioned project serialization and migrations.

Prefer interfaces at subsystem boundaries. The core engine should be testable without a GUI, MIDI hardware, or audio device.

## Real-time safety

Never perform these operations on a real-time audio callback or other deadline-critical thread:
- blocking mutex acquisition or waiting;
- disk or network I/O;
- synchronous logging;
- unbounded container growth or traversal;
- UI calls;
- waiting for worker threads;
- memory allocation with unpredictable latency.

Use bounded queues, preallocated buffers, immutable snapshots, and explicit overflow policies. Note Off and Panic events must receive priority over non-critical output when capacity is exhausted.

## Code quality

- Use modern C++ (target C++20 unless the selected JUCE release imposes a documented constraint).
- Use RAII, strong types for musical units, and explicit ownership.
- Avoid global mutable state.
- Make random behavior seedable for deterministic tests.
- Document thread ownership for every shared object.
- Keep engine behavior independent of frame rate and UI refresh rate.
- Pin the JUCE version and dependencies; do not silently track a moving branch.

## Required completion report

Use `docs/AGENT_COMPLETION_REPORT_TEMPLATE.md`.
