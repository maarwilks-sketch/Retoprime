if(NOT DEFINED QUADRIFLOW_BINARY_DIR OR NOT DEFINED STAGED_HELPER)
    message(FATAL_ERROR "QUADRIFLOW_BINARY_DIR and STAGED_HELPER are required")
endif()

set(candidates
    "${QUADRIFLOW_BINARY_DIR}/quadriflow${EXECUTABLE_SUFFIX}"
    "${QUADRIFLOW_BINARY_DIR}/Release/quadriflow${EXECUTABLE_SUFFIX}"
)
set(source_helper "")
foreach(candidate IN LISTS candidates)
    if(EXISTS "${candidate}")
        set(source_helper "${candidate}")
        break()
    endif()
endforeach()
if(source_helper STREQUAL "")
    message(FATAL_ERROR "The built QuadriFlow helper was not found in a flat or Release output directory")
endif()

get_filename_component(staging_directory "${STAGED_HELPER}" DIRECTORY)
file(MAKE_DIRECTORY "${staging_directory}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${source_helper}" "${STAGED_HELPER}"
    COMMAND_ERROR_IS_FATAL ANY
)
