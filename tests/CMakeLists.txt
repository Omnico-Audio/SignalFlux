# Unit tests: link the core only, never JUCE or GUI (SF-001 acceptance).
# Test and SECTION names must be ASCII: CTest passes them to the test binary on the
# command line, and the Windows console code page mangles non-ASCII characters.
add_executable(signalflux_tests
    unit/TypesTests.cpp
    unit/ScaleTests.cpp
    unit/GeometryTests.cpp
    unit/ValidationTests.cpp
    unit/EngineComponentTests.cpp
    unit/SequencerTests.cpp
    unit/GoldenLogTests.cpp
    unit/NoteResolutionTests.cpp
    unit/BranchProbabilityRepeatTests.cpp
    unit/LogicTerminatorTests.cpp
    unit/PersistenceTests.cpp
    unit/MidiTests.cpp
    unit/SynthTests.cpp
    unit/EditingTests.cpp
    unit/SwingHumanizeTests.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(signalflux_tests PRIVATE SignalFlux::core SignalFlux::persistence SignalFlux::audio Catch2::Catch2WithMain Threads::Threads)
signalflux_apply_strict_warnings(signalflux_tests)
target_compile_definitions(signalflux_tests PRIVATE
    SIGNALFLUX_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden"
    SIGNALFLUX_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples"
    # MSVC deprecates std::getenv (C4996); the tests only read SIGNALFLUX_UPDATE_GOLDEN.
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

include(Catch)
catch_discover_tests(signalflux_tests)

if(SIGNALFLUX_BUILD_RENDER)
    add_test(NAME render.version COMMAND signalflux-render --version)
    add_test(NAME render.self_check COMMAND signalflux-render --self-check)
    foreach(example first-loop sequential-hub branches gatekeeper instant-bloom polyrhythm wanderer)
        add_test(NAME render.golden.${example}
            COMMAND ${CMAKE_COMMAND}
                -DRENDER=$<TARGET_FILE:signalflux-render>
                -DEXAMPLE=${example}
                -DBEATS=32
                -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden/${example}.log
                -DOUT=${CMAKE_CURRENT_BINARY_DIR}/render-${example}.log
                -P ${CMAKE_CURRENT_SOURCE_DIR}/CompareRenderOutput.cmake)
        add_test(NAME render.project.${example}
            COMMAND ${CMAKE_COMMAND}
                -DRENDER=$<TARGET_FILE:signalflux-render>
                -DPROJECT=${PROJECT_SOURCE_DIR}/examples/${example}.sflux
                -DBEATS=32
                -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden/${example}.log
                -DOUT=${CMAKE_CURRENT_BINARY_DIR}/render-project-${example}.log
                -P ${CMAKE_CURRENT_SOURCE_DIR}/CompareRenderOutput.cmake)
    endforeach()
endif()
