if(NOT DEFINED QUADRIFLOW_SOURCE_DIR)
    message(FATAL_ERROR "QUADRIFLOW_SOURCE_DIR is required")
endif()

set(quadriflow_cmake_lists "${QUADRIFLOW_SOURCE_DIR}/CMakeLists.txt")
if(NOT EXISTS "${quadriflow_cmake_lists}")
    message(FATAL_ERROR "QuadriFlow CMakeLists.txt was not found at ${quadriflow_cmake_lists}")
endif()

file(READ "${quadriflow_cmake_lists}" contents)
set(old_release_flags
    "set(CMAKE_CXX_FLAGS_RELEASE \"-O3\")  # enable assert"
)
set(windows_release_flags
    "set(CMAKE_CXX_FLAGS_RELEASE \"/MD /O2 /Ob2 /DNDEBUG\")"
)

string(FIND "${contents}" "${windows_release_flags}" already_patched)
if(NOT already_patched EQUAL -1)
    message(STATUS "QuadriFlow MSVC release flags are already patched")
    return()
endif()

string(FIND "${contents}" "${old_release_flags}" old_flags_position)
if(old_flags_position EQUAL -1)
    message(FATAL_ERROR
        "The pinned QuadriFlow release flag was not found; refusing to patch unknown source"
    )
endif()

set(replacement
    "if(MSVC)\n"
    "    set(CMAKE_CXX_FLAGS_RELEASE \"/MD /O2 /Ob2 /DNDEBUG\")\n"
    "else()\n"
    "    set(CMAKE_CXX_FLAGS_RELEASE \"-O3\")  # enable assert\n"
    "endif()"
)
string(REPLACE "${old_release_flags}" "${replacement}" contents "${contents}")
file(WRITE "${quadriflow_cmake_lists}" "${contents}")
message(STATUS "Patched QuadriFlow release flags for the MSVC runtime")
