# signalflux_core: domain + engine. Pure C++20, no JUCE, no devices (ARCHITECTURE §2).
add_library(signalflux_core STATIC
    domain/Types.h
    domain/Scale.h
    domain/Geometry.h
    domain/Project.h
    domain/Project.cpp
    domain/Validation.h
    domain/Validation.cpp
)
add_library(SignalFlux::core ALIAS signalflux_core)
target_include_directories(signalflux_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(signalflux_core PUBLIC cxx_std_20)
target_compile_definitions(signalflux_core PUBLIC SIGNALFLUX_VERSION_STRING="${PROJECT_VERSION}")
signalflux_apply_strict_warnings(signalflux_core)

if(SIGNALFLUX_BUILD_RENDER)
    add_subdirectory(render)
endif()

if(SIGNALFLUX_BUILD_APP)
    add_subdirectory(app)
endif()
