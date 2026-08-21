include(ExternalProject)

set(RETOPRIME_QUADRIFLOW_COMMIT
    "810b7a0967c35b0dc85b4464e3835e26a756c967"
    CACHE INTERNAL "Pinned QuadriFlow source revision"
)
set(RETOPRIME_QUADRIFLOW_BOOST_INCLUDE_DIR "" CACHE PATH
    "Optional Boost include directory for a developer-only external build"
)
set(RETOPRIME_QUADRIFLOW_EIGEN_INCLUDE_DIR "" CACHE PATH
    "Optional Eigen include directory for a developer-only external build"
)

function(retoprime_add_quadriflow_external application_target)
    if(NOT TARGET "${application_target}")
        message(FATAL_ERROR "Unknown RETOPRIME application target: ${application_target}")
    endif()

    set(quadriflow_cmake_args
        -DCMAKE_BUILD_TYPE=Release
        -DBUILD_FREE_LICENSE=ON
        -DBUILD_GUROBI=OFF
        -DBUILD_LOG=OFF
        -DBUILD_OPENMP=OFF
        -DBUILD_TBB=OFF
        -DBUILD_PERFORMANCE_TEST=OFF
    )

    if(DEFINED CMAKE_MAKE_PROGRAM AND NOT CMAKE_MAKE_PROGRAM STREQUAL "")
        list(APPEND quadriflow_cmake_args
            "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
        )
    endif()

    if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_TOOLCHAIN_FILE STREQUAL "")
        list(APPEND quadriflow_cmake_args
            "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
        )
    endif()
    if(DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET STREQUAL "")
        list(APPEND quadriflow_cmake_args
            "-DVCPKG_TARGET_TRIPLET:STRING=${VCPKG_TARGET_TRIPLET}"
        )
    endif()
    if(DEFINED VCPKG_INSTALLED_DIR AND NOT VCPKG_INSTALLED_DIR STREQUAL "")
        list(APPEND quadriflow_cmake_args
            "-DVCPKG_INSTALLED_DIR:PATH=${VCPKG_INSTALLED_DIR}"
        )
    endif()
    foreach(path_variable IN ITEMS CMAKE_PREFIX_PATH CMAKE_FIND_ROOT_PATH CMAKE_MODULE_PATH)
        if(DEFINED ${path_variable} AND NOT "${${path_variable}}" STREQUAL "")
            string(REPLACE ";" "|" propagated_paths "${${path_variable}}")
            list(APPEND quadriflow_cmake_args
                "-D${path_variable}:PATH=${propagated_paths}"
            )
        endif()
    endforeach()
    list(APPEND quadriflow_cmake_args -DVCPKG_MANIFEST_MODE=OFF)
    if(NOT RETOPRIME_QUADRIFLOW_BOOST_INCLUDE_DIR STREQUAL "")
        list(APPEND quadriflow_cmake_args
            "-DBoost_INCLUDE_DIR:PATH=${RETOPRIME_QUADRIFLOW_BOOST_INCLUDE_DIR}"
        )
    endif()
    if(NOT RETOPRIME_QUADRIFLOW_EIGEN_INCLUDE_DIR STREQUAL "")
        list(APPEND quadriflow_cmake_args
            "-DEIGEN_INCLUDE_DIR:PATH=${RETOPRIME_QUADRIFLOW_EIGEN_INCLUDE_DIR}"
        )
    endif()

    set(quadriflow_binary_dir
        "${CMAKE_BINARY_DIR}/quadriflow/src/quadriflow_external-build"
    )
    set(staged_helper
        "$<TARGET_FILE_DIR:${application_target}>/engine/retoprime-quads${CMAKE_EXECUTABLE_SUFFIX}"
    )
    set(external_staged_helper
        "${CMAKE_BINARY_DIR}/quadriflow/stage/retoprime-quads${CMAKE_EXECUTABLE_SUFFIX}"
    )

    ExternalProject_Add(quadriflow_external
        EXCLUDE_FROM_ALL TRUE
        PREFIX "${CMAKE_BINARY_DIR}/quadriflow"
        GIT_REPOSITORY "https://github.com/hjwdzh/QuadriFlow.git"
        GIT_TAG "${RETOPRIME_QUADRIFLOW_COMMIT}"
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
        UPDATE_DISCONNECTED TRUE
        LIST_SEPARATOR "|"
        CMAKE_ARGS ${quadriflow_cmake_args}
        BUILD_COMMAND
            "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release --target quadriflow
        INSTALL_COMMAND
            "${CMAKE_COMMAND}"
                "-DQUADRIFLOW_BINARY_DIR=${quadriflow_binary_dir}"
                "-DSTAGED_HELPER=${external_staged_helper}"
                "-DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StageQuadriFlow.cmake"
        INSTALL_BYPRODUCTS "${external_staged_helper}"
        USES_TERMINAL_DOWNLOAD TRUE
        USES_TERMINAL_BUILD TRUE
    )

    add_custom_target(quadriflow_stage_helper
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:${application_target}>/engine"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${external_staged_helper}"
            "${staged_helper}"
        DEPENDS quadriflow_external
        VERBATIM
    )

    set(RETOPRIME_QUADRIFLOW_STAGED_HELPER
        "${staged_helper}"
        PARENT_SCOPE
    )
    set(RETOPRIME_QUADRIFLOW_STAGE_TARGET
        quadriflow_stage_helper
        PARENT_SCOPE
    )
endfunction()
