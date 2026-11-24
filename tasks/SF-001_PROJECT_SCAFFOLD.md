# SF-001 — Project Scaffold

Create the clean C++ / JUCE / CMake repository and a minimal standalone desktop app.

Acceptance:
- CMake configures on Windows, macOS, Linux.
- JUCE revision is pinned.
- `juce_add_gui_app` is used.
- No plugin target is created.
- CI builds all supported targets.
- Unit test target exists independently of the GUI.
