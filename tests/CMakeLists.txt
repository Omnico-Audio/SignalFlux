# Unit tests: link the core only, never JUCE or GUI (SF-001 acceptance).
add_executable(signalflux_tests
    unit/TypesTests.cpp
    unit/ScaleTests.cpp
    unit/GeometryTests.cpp
    unit/ValidationTests.cpp
)
target_link_libraries(signalflux_tests PRIVATE SignalFlux::core Catch2::Catch2WithMain)
signalflux_apply_strict_warnings(signalflux_tests)

include(Catch)
catch_discover_tests(signalflux_tests)

if(SIGNALFLUX_BUILD_RENDER)
    add_test(NAME render.version COMMAND signalflux-render --version)
    add_test(NAME render.self_check COMMAND signalflux-render --self-check)
endif()
