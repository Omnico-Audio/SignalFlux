# signalflux_core: domain + engine. Pure C++20, no JUCE, no devices (ARCHITECTURE §2).
add_library(signalflux_core STATIC
    domain/Types.h
    domain/Scale.h
    domain/Geometry.h
    domain/Project.h
    domain/Project.cpp
    domain/Validation.h
    domain/Validation.cpp
    domain/Examples.h
    domain/Examples.cpp
    engine/LegTiming.h
    engine/Random.h
    engine/NoteResolution.h
    engine/NoteResolution.cpp
    engine/RuntimeGraph.h
    engine/RuntimeGraph.cpp
    engine/EngineEvent.h
    engine/EngineEvent.cpp
    engine/ActiveNoteRegistry.h
    engine/ActiveNoteRegistry.cpp
    engine/LogicEngine.h
    engine/LogicEngine.cpp
    engine/Sequencer.h
    engine/Sequencer.cpp
    engine/EventLog.h
    engine/EventLog.cpp
    midi/MidiBytes.h
    midi/MidiBytes.cpp
    midi/PlaybackClock.h
    midi/PlaybackClock.cpp
    midi/SpscQueue.h
    midi/MidiInputRouter.h
    midi/MidiInputRouter.cpp
)
add_library(SignalFlux::core ALIAS signalflux_core)
target_include_directories(signalflux_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(signalflux_core PUBLIC cxx_std_20)
target_compile_definitions(signalflux_core PUBLIC SIGNALFLUX_VERSION_STRING="${PROJECT_VERSION}")
signalflux_apply_strict_warnings(signalflux_core)

# signalflux_persistence: project files (SF-010). Depends on the core and a header-only JSON library.
add_library(signalflux_persistence STATIC
    persistence/ProjectSerializer.h
    persistence/ProjectSerializer.cpp)
add_library(SignalFlux::persistence ALIAS signalflux_persistence)
target_link_libraries(signalflux_persistence PUBLIC SignalFlux::core)
# Treat the JSON headers as system headers so our strict warnings apply to our code only.
target_include_directories(signalflux_persistence SYSTEM PRIVATE
    $<TARGET_PROPERTY:nlohmann_json::nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>)
signalflux_apply_strict_warnings(signalflux_persistence)

# signalflux_audio: optional monitoring synth (SF-008). No JUCE; fed by the app's audio callback.
add_library(signalflux_audio STATIC audio/Synth.h audio/Synth.cpp)
add_library(SignalFlux::audio ALIAS signalflux_audio)
target_include_directories(signalflux_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(signalflux_audio PUBLIC cxx_std_20)
signalflux_apply_strict_warnings(signalflux_audio)

if(SIGNALFLUX_BUILD_RENDER)
    add_subdirectory(render)
endif()

if(SIGNALFLUX_BUILD_APP)
    add_subdirectory(app)
endif()
