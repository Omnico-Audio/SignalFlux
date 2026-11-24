# SignalFlux desktop application (standalone; no plugin targets — AGENTS.md).
juce_add_gui_app(SignalFlux
    PRODUCT_NAME "SignalFlux"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "SignalFlux"
    BUNDLE_ID "org.signalflux.app"
    DOCUMENT_EXTENSIONS sflux
    MICROPHONE_PERMISSION_ENABLED FALSE)

target_sources(SignalFlux PRIVATE
    Main.cpp
    MainComponent.h
    MainComponent.cpp
    EngineHost.h
    EngineHost.cpp
    SynthOutput.h
    SynthOutput.cpp)

target_compile_definitions(SignalFlux PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SignalFlux,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SignalFlux,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SignalFlux
    PRIVATE
        SignalFlux::core
        SignalFlux::persistence
        SignalFlux::audio
        juce::juce_gui_basics
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

if(SIGNALFLUX_WITH_LINK)
    target_link_libraries(SignalFlux PRIVATE Ableton::Link)
    target_compile_definitions(SignalFlux PRIVATE SIGNALFLUX_WITH_LINK=1)
else()
    target_compile_definitions(SignalFlux PRIVATE SIGNALFLUX_WITH_LINK=0)
endif()

signalflux_apply_app_warnings(SignalFlux)
