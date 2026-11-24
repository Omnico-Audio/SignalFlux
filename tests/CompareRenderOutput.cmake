# Runs signalflux-render for one example and compares its stdout byte for byte
# with the golden log. Guards the renderer's own output path (for example
# Windows text-mode stdout turning '\n' into "\r\n").
#
# Inputs: RENDER (executable), EXAMPLE, BEATS, GOLDEN (file), OUT (scratch file).

execute_process(
    COMMAND "${RENDER}" --example "${EXAMPLE}" --beats "${BEATS}"
    OUTPUT_FILE "${OUT}"
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "signalflux-render exited with ${rc}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}" "${GOLDEN}" RESULT_VARIABLE diff)
if(NOT diff EQUAL 0)
    message(FATAL_ERROR "signalflux-render output differs from ${GOLDEN}")
endif()
