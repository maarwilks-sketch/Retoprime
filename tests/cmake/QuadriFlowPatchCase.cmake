if(NOT DEFINED RETOPRIME_SOURCE_DIR OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "RETOPRIME_SOURCE_DIR and TEST_ROOT are required")
endif()
file(MAKE_DIRECTORY "${TEST_ROOT}/src")
file(WRITE "${TEST_ROOT}/CMakeLists.txt"
    "set(CMAKE_CXX_FLAGS_RELEASE \"-O3\")  # enable assert\n")
file(WRITE "${TEST_ROOT}/src/main.cpp"
    "#include <stdlib.h>\nint main() { assert(1); return 0; }\n")
foreach(pass RANGE 1 2)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DQUADRIFLOW_SOURCE_DIR=${TEST_ROOT}"
        -P "${RETOPRIME_SOURCE_DIR}/cmake/PatchQuadriFlow.cmake"
        COMMAND_ERROR_IS_FATAL ANY)
endforeach()
# Actually parse and execute the patched CMake, not just search its text.
set(MSVC TRUE)
include("${TEST_ROOT}/CMakeLists.txt")
if(NOT CMAKE_CXX_FLAGS_RELEASE STREQUAL "/MD /O2 /Ob2 /DNDEBUG")
    message(FATAL_ERROR "MSVC Release runtime flags were lost")
endif()
set(MSVC FALSE)
include("${TEST_ROOT}/CMakeLists.txt")
if(NOT CMAKE_CXX_FLAGS_RELEASE STREQUAL "-O3")
    message(FATAL_ERROR "Non-MSVC flags changed")
endif()
file(READ "${TEST_ROOT}/src/main.cpp" main_source)
if(NOT main_source MATCHES "#include <cassert>")
    message(FATAL_ERROR "QuadriFlow main.cpp still lacks its assert declaration")
endif()
