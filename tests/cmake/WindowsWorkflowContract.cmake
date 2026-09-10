if(NOT DEFINED WORKFLOW)
    message(FATAL_ERROR "WORKFLOW is required")
endif()

file(READ "${WORKFLOW}" contents)
if(contents MATCHES "env:[\r\n ]+VCPKG_ROOT:")
    message(FATAL_ERROR "The job environment must not predefine VCPKG_ROOT")
endif()
foreach(required IN ITEMS
        "RETOPRIME_VCPKG_ROOT:"
        "git clone https://github.com/microsoft/vcpkg.git $env:RETOPRIME_VCPKG_ROOT"
        "VCPKG_ROOT=$env:RETOPRIME_VCPKG_ROOT"
        "GITHUB_ENV"
        "Re-stage a deleted helper incrementally"
        "Remove-Item $stagedHelper"
        "cmake --build build/task-4-windows --target retoprime_release")
    string(FIND "${contents}" "${required}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing Windows workflow contract text: ${required}")
    endif()
endforeach()

string(FIND "${contents}" "Set up MSVC developer command prompt" msvc_position)
string(FIND "${contents}" "VCPKG_ROOT=$env:RETOPRIME_VCPKG_ROOT" publish_position)
if(msvc_position EQUAL -1 OR publish_position EQUAL -1 OR
   publish_position LESS msvc_position)
    message(FATAL_ERROR "VCPKG_ROOT must be published only after MSVC environment setup")
endif()
