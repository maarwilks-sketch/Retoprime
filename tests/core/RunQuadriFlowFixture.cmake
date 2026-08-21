if(NOT DEFINED HELPER OR NOT EXISTS "${HELPER}")
    message(FATAL_ERROR "The staged QuadriFlow helper does not exist: ${HELPER}")
endif()

file(REMOVE "${OUTPUT}")
execute_process(
    COMMAND "${HELPER}" -i "${INPUT}" -o "${OUTPUT}" -f 6 -sharp
    RESULT_VARIABLE quadriflow_result
    OUTPUT_VARIABLE quadriflow_stdout
    ERROR_VARIABLE quadriflow_stderr
    TIMEOUT 120
)

if(NOT quadriflow_result STREQUAL "0")
    message(FATAL_ERROR
        "QuadriFlow failed with exit code ${quadriflow_result}.\n"
        "stdout:\n${quadriflow_stdout}\n"
        "stderr:\n${quadriflow_stderr}"
    )
endif()

if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "QuadriFlow exited successfully but did not create ${OUTPUT}")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "RETOPRIME_REAL_OUTPUT=${OUTPUT}"
        "${TEST_EXECUTABLE}" "[.real-engine]"
    RESULT_VARIABLE validation_result
    OUTPUT_VARIABLE validation_stdout
    ERROR_VARIABLE validation_stderr
)

if(NOT validation_result STREQUAL "0")
    message(FATAL_ERROR
        "QuadriFlow output validation failed.\n"
        "stdout:\n${validation_stdout}\n"
        "stderr:\n${validation_stderr}"
    )
endif()
