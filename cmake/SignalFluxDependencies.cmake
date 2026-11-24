# Third-party dependencies, pinned to exact commits (AGENTS.md: never track a moving branch).
#
# To build offline or from a local checkout, point FetchContent at it, e.g.
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
#
# | Dependency   | Release      | Commit                                   | Licence              |
# |--------------|--------------|------------------------------------------|----------------------|
# | JUCE         | 9.0.6        | 654bdbc7a9db0dae90849b44abe91f339e9f0768 | AGPLv3 / commercial  |
# | Catch2       | v3.16.1      | 08092139210881f01a34e60c5f2fd6afa0ea4024 | BSL-1.0              |
# | Ableton Link | Link-4.1.1   | 5ba907ae027a8c2ec8824463e37296dcefc5e9be | GPLv2+ / proprietary |

include(FetchContent)

set(SIGNALFLUX_JUCE_COMMIT   654bdbc7a9db0dae90849b44abe91f339e9f0768)
set(SIGNALFLUX_CATCH2_COMMIT 08092139210881f01a34e60c5f2fd6afa0ea4024)
set(SIGNALFLUX_LINK_COMMIT   5ba907ae027a8c2ec8824463e37296dcefc5e9be)

if(SIGNALFLUX_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        ${SIGNALFLUX_CATCH2_COMMIT})
    FetchContent_MakeAvailable(Catch2)
endif()

if(SIGNALFLUX_BUILD_APP)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${SIGNALFLUX_JUCE_COMMIT})
    FetchContent_MakeAvailable(JUCE)

    if(SIGNALFLUX_WITH_LINK)
        FetchContent_Declare(AbletonLink
            GIT_REPOSITORY https://github.com/Ableton/link.git
            GIT_TAG        ${SIGNALFLUX_LINK_COMMIT}
            GIT_SUBMODULES_RECURSE ON)
        FetchContent_MakeAvailable(AbletonLink)
        include("${abletonlink_SOURCE_DIR}/AbletonLinkConfig.cmake")
    endif()
endif()
