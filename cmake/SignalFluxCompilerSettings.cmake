# Compiler settings shared by SignalFlux-owned targets.
#
# Determinism (DECISIONS D25): no fast-math anywhere; warnings are errors on
# engine and test code so every compiler sees the same clean source.

function(signalflux_apply_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /fp:precise /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wconversion -Wsign-conversion -Wshadow
            -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
            -fno-fast-math)
    endif()
endfunction()

# Lighter settings for targets that include JUCE headers (JUCE is not warning-clean
# under -Wconversion). Still no fast-math.
function(signalflux_apply_app_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /fp:precise /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -fno-fast-math)
    endif()
endfunction()
