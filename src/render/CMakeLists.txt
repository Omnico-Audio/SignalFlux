# signalflux-render: headless engine runner (DECISIONS D25, D28).
# Links the core only — no GUI, audio, or MIDI devices — so it runs identically in CI on every platform.
add_executable(signalflux-render main.cpp)
target_link_libraries(signalflux-render PRIVATE SignalFlux::core SignalFlux::persistence)
signalflux_apply_strict_warnings(signalflux-render)
