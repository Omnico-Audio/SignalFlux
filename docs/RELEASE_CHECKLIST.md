# Beta Release Checklist (SF-010)

## Automated (CI must be green)

- [ ] `main` CI green on macOS, Windows and the Linux engine job.
- [ ] Golden logs unchanged, or changes reviewed and intended (`tests/fixtures/golden/`).
- [ ] Example projects load and render from file (`render.project.*` tests).

## Cut the release

1. Update `project(SignalFlux VERSION ...)` in `CMakeLists.txt` if the version changes.
2. Tag and push: `git tag v0.1.0-beta.1 && git push origin v0.1.0-beta.1`.
3. The **Release** workflow builds, tests and packages:
   - `SignalFlux-<tag>-macOS-arm64.dmg` (app, Examples, licence, READ ME FIRST; ad-hoc signed)
   - `SignalFlux-<tag>-Windows-x64.zip` (exe, Examples, licence, READ ME FIRST)
   and publishes them as a GitHub pre-release.
4. AGPL: the repository must be public (or shared with every tester) when packages go out. The release notes point to the tagged source.

## Not automated yet

- **macOS notarization and Developer ID signing** (needs an Apple Developer account): replace the ad-hoc `codesign` step with Developer ID signing, then `notarytool submit --wait` and `stapler staple`.
- **Windows code signing** (needs a certificate) and an installer (e.g. Inno Setup). The beta ships as a zip.
- **Intel Mac build**: add `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` once Intel support is intended.

## Manual checks before sending to testers

Use `docs/HARDWARE_VALIDATION.md` and record results there.

- [ ] App launches on a clean macOS and a clean Windows machine.
- [ ] First run shows the First Loop example; Space plays; Synth toggle makes sound.
- [ ] MIDI out to a DAW (virtual port on macOS, loopMIDI on Windows) and to one hardware synth.
- [ ] MIDI Clock follows tempo; Stop/Continue keep the DAW in step.
- [ ] Panic and Stop leave no stuck notes on any of the 16 channels.
- [ ] Unplug the MIDI device while playing: no crash; re-select it and playback continues.
- [ ] Save, quit, reopen: the project is identical. Force-quit while editing: relaunch offers recovery, and recovery does not start playback.
- [ ] Capture writes a .mid that imports into a DAW at the right tempo.
- [ ] Ableton Link: tempo follows Live in both directions.
