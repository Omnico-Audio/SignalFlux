# SignalFlux — grid-based, non-linear MIDI sequencer (standalone desktop app).
# Copyright (C) 2026 The SignalFlux contributors. Licensed under the GNU AGPLv3.

cmake_minimum_required(VERSION 3.24)

project(SignalFlux
    VERSION 0.1.0
    DESCRIPTION "Grid-based, non-linear MIDI sequencer"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    # Apple Silicon is the release target; universal builds can be enabled from the command line.
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
endif()

option(SIGNALFLUX_BUILD_APP   "Build the JUCE desktop application"               ON)
option(SIGNALFLUX_BUILD_TESTS "Build the unit test target (no GUI dependency)"   ON)
option(SIGNALFLUX_BUILD_RENDER "Build the headless signalflux-render tool"       ON)
option(SIGNALFLUX_WITH_LINK   "Build with Ableton Link tempo sync (GPLv2+)"      ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SignalFluxCompilerSettings)
include(SignalFluxDependencies)

add_subdirectory(src)

if(SIGNALFLUX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "SignalFlux ${PROJECT_VERSION}: app=${SIGNALFLUX_BUILD_APP} tests=${SIGNALFLUX_BUILD_TESTS} "
               "render=${SIGNALFLUX_BUILD_RENDER} link=${SIGNALFLUX_WITH_LINK}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}; CMake ${CMAKE_VERSION}")
