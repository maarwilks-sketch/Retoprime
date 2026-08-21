cmake_minimum_required(VERSION 3.28)

project(
    RETOPRIME
    VERSION 1.0.0
    DESCRIPTION "Native RETOPRIME application foundation"
    LANGUAGES CXX
)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)

option(
    RETOPRIME_ENABLE_QUADRIFLOW_EXTERNAL
    "Fetch and build the pinned free-license QuadriFlow helper"
    OFF
)
option(
    RETOPRIME_ALLOW_NO_QPROCESS_FOR_TESTS
    "Allow the process stub in a non-shipping unit-test-only build"
    OFF
)

find_package(Catch2 CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Widgets)
find_package(assimp CONFIG REQUIRED)

get_target_property(RETOPRIME_QT_CORE_FEATURES Qt6::Core QT_ENABLED_PUBLIC_FEATURES)
list(FIND RETOPRIME_QT_CORE_FEATURES process RETOPRIME_QT_PROCESS_FEATURE_INDEX)
list(FIND RETOPRIME_QT_CORE_FEATURES thread RETOPRIME_QT_THREAD_FEATURE_INDEX)
set(RETOPRIME_QT_HAS_PROCESS FALSE)
set(RETOPRIME_QT_HAS_THREAD FALSE)
if(NOT RETOPRIME_QT_PROCESS_FEATURE_INDEX EQUAL -1)
    set(RETOPRIME_QT_HAS_PROCESS TRUE)
endif()
if(NOT RETOPRIME_QT_THREAD_FEATURE_INDEX EQUAL -1)
    set(RETOPRIME_QT_HAS_THREAD TRUE)
endif()

include(QtRuntimeContract)
retoprime_enforce_qt_runtime_contract(
    "${RETOPRIME_QT_HAS_PROCESS}"
    "${RETOPRIME_QT_HAS_THREAD}"
)

add_library(retoprime_core
    src/core/Mesh.cpp
    src/core/MeshIO.cpp
    src/core/RetopoEngine.cpp
    src/core/RetopoSettings.cpp
)

retoprime_set_project_warnings(retoprime_core)

target_include_directories(retoprime_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(retoprime_core
    PUBLIC
        Eigen3::Eigen
        Qt6::Core
        assimp::assimp
)

if(WIN32)
    target_link_libraries(retoprime_core PRIVATE Advapi32)
endif()

add_executable(RETOPRIME
    src/app/main.cpp
)

set_target_properties(RETOPRIME PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/app"
)

if(RETOPRIME_ENABLE_QUADRIFLOW_EXTERNAL)
    include(QuadriFlowExternal)
    retoprime_add_quadriflow_external(RETOPRIME)
    add_custom_target(retoprime_release
        DEPENDS
            RETOPRIME
            ${RETOPRIME_QUADRIFLOW_STAGE_TARGET}
    )
endif()

retoprime_set_project_warnings(RETOPRIME)

target_link_libraries(RETOPRIME
    PRIVATE
        Qt6::Widgets
)

add_executable(AppMetadataTests
    tests/core/AppMetadataTests.cpp
    src/app/main.cpp
)

retoprime_set_project_warnings(AppMetadataTests)

target_compile_definitions(AppMetadataTests
    PRIVATE
        RETOPRIME_NO_APP_MAIN
)

target_link_libraries(AppMetadataTests
    PRIVATE
        Catch2::Catch2WithMain
        Qt6::Widgets
)

include(Catch)
catch_discover_tests(AppMetadataTests TEST_PREFIX "AppMetadata.")

add_executable(MeshIOTests
    tests/core/MeshIOTests.cpp
)

retoprime_set_project_warnings(MeshIOTests)

target_link_libraries(MeshIOTests
    PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        retoprime_core
)

catch_discover_tests(MeshIOTests TEST_PREFIX "MeshIO.")

add_executable(RetopoSettingsTests
    tests/core/RetopoSettingsTests.cpp
)

retoprime_set_project_warnings(RetopoSettingsTests)

target_link_libraries(RetopoSettingsTests
    PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        retoprime_core
)

catch_discover_tests(RetopoSettingsTests TEST_PREFIX "RetopoSettings.")

add_executable(RetopoEngineTests
    tests/core/RetopoEngineTests.cpp
)

retoprime_set_project_warnings(RetopoEngineTests)

target_link_libraries(RetopoEngineTests
    PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        retoprime_core
)

catch_discover_tests(RetopoEngineTests TEST_PREFIX "RetopoEngine.")

if(RETOPRIME_QT_HAS_PROCESS AND RETOPRIME_QT_HAS_THREAD)
    add_executable(RetopoLongRunningHelper
        tests/helpers/RetopoLongRunningHelper.cpp
    )

    add_executable(RetopoProcessIntegrationTests
        tests/core/RetopoProcessIntegrationMain.cpp
        tests/core/RetopoProcessIntegrationTests.cpp
    )
    retoprime_set_project_warnings(RetopoProcessIntegrationTests)
    target_link_libraries(RetopoProcessIntegrationTests
        PRIVATE
            Catch2::Catch2
            Qt6::Core
            retoprime_core
    )
    target_compile_definitions(RetopoProcessIntegrationTests PRIVATE
        RETOPRIME_LONG_HELPER="$<TARGET_FILE:RetopoLongRunningHelper>"
    )
    if(WIN32)
        target_link_libraries(RetopoProcessIntegrationTests PRIVATE Advapi32)
    endif()
    if(RETOPRIME_ENABLE_QUADRIFLOW_EXTERNAL)
        target_compile_definitions(RetopoProcessIntegrationTests PRIVATE
            RETOPRIME_REAL_HELPER="${RETOPRIME_QUADRIFLOW_STAGED_HELPER}"
        )
    endif()
    catch_discover_tests(RetopoProcessIntegrationTests
        TEST_PREFIX "RetopoProcess."
        PROPERTIES LABELS "process-enabled"
    )
endif()

add_test(
    NAME CMake.QtRuntimeContract.TestOptOut
    COMMAND "${CMAKE_COMMAND}"
        -DRETOPRIME_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DBUILD_TESTING=ON
        -DRETOPRIME_ALLOW_NO_QPROCESS_FOR_TESTS=ON
        -DRETOPRIME_ENABLE_QUADRIFLOW_EXTERNAL=OFF
        -DCMAKE_BUILD_TYPE=Debug
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/QtRuntimeContractCase.cmake
)

foreach(contract_case IN ITEMS Normal Release External)
    set(contract_arguments
        -DRETOPRIME_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DBUILD_TESTING=ON
        -DRETOPRIME_ALLOW_NO_QPROCESS_FOR_TESTS=ON
        -DRETOPRIME_ENABLE_QUADRIFLOW_EXTERNAL=OFF
        -DCMAKE_BUILD_TYPE=Debug
    )
    if(contract_case STREQUAL "Normal")
        list(APPEND contract_arguments -DRETOPRIME_ALLOW_NO_QPROCESS_FOR_TESTS=OFF)
    elseif(contract_case STREQUAL "Release")
        list(APPEND contract_arguments -DCMAKE_BUILD_TYPE=Release)
    else()
        list(APPEND contract_arguments -DRETOPRIME_ENABLE_QUADRIFLOW_EXTERNAL=ON)
    endif()
    add_test(
        NAME CMake.QtRuntimeContract.Reject${contract_case}
        COMMAND "${CMAKE_COMMAND}" ${contract_arguments}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/QtRuntimeContractCase.cmake
    )
    set_tests_properties(CMake.QtRuntimeContract.Reject${contract_case}
        PROPERTIES WILL_FAIL TRUE)
endforeach()

foreach(output_layout IN ITEMS Flat Release)
    add_test(
        NAME CMake.QuadriFlowStage.${output_layout}
        COMMAND "${CMAKE_COMMAND}"
            -DRETOPRIME_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/stage-test-${output_layout}
            -DOUTPUT_LAYOUT=${output_layout}
            -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/StageQuadriFlowCase.cmake
    )
endforeach()

add_test(
    NAME CMake.WindowsWorkflowContract
    COMMAND "${CMAKE_COMMAND}"
        -DWORKFLOW=${CMAKE_CURRENT_SOURCE_DIR}/.github/workflows/task-4-windows-validation.yml
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/WindowsWorkflowContract.cmake
)

add_custom_target(RETOPRIME_tests
    DEPENDS
        AppMetadataTests
        MeshIOTests
        RetopoEngineTests
        RetopoSettingsTests
)

if(TARGET RetopoProcessIntegrationTests)
    add_dependencies(RETOPRIME_tests RetopoProcessIntegrationTests RetopoLongRunningHelper)
endif()

if(RETOPRIME_ENABLE_QUADRIFLOW_EXTERNAL)
    add_test(
        NAME RetopoEngine.RealQuadriFlowCube
        COMMAND
            "${CMAKE_COMMAND}"
            "-DHELPER=${RETOPRIME_QUADRIFLOW_STAGED_HELPER}"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cube-triangulated.obj"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/quadriflow-real-cube-output.obj"
            "-DTEST_EXECUTABLE=$<TARGET_FILE:RetopoEngineTests>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/RunQuadriFlowFixture.cmake"
    )
endif()
