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

find_package(Catch2 CONFIG REQUIRED)
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Widgets)

add_executable(RETOPRIME
    src/app/main.cpp
)

set_target_properties(RETOPRIME PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/app"
)

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
