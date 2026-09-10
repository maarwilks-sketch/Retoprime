function(retoprime_enforce_qt_runtime_contract has_process has_thread)
    if(has_process AND has_thread)
        return()
    endif()

    set(opt_out_allowed TRUE)
    if(NOT RETOPRIME_ALLOW_NO_QPROCESS_FOR_TESTS)
        set(opt_out_allowed FALSE)
    endif()
    if(NOT BUILD_TESTING OR RETOPRIME_ENABLE_QUADRIFLOW_EXTERNAL)
        set(opt_out_allowed FALSE)
    endif()
    if(CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE MATCHES "^[Rr]elease$")
        set(opt_out_allowed FALSE)
    endif()

    if(NOT opt_out_allowed)
        message(FATAL_ERROR
            "RETOPRIME requires a Qt Core build with process and thread support. "
            "RETOPRIME_ALLOW_NO_QPROCESS_FOR_TESTS is permitted only for a "
            "single-config, non-Release BUILD_TESTING build with the external engine disabled."
        )
    endif()

    message(WARNING
        "Building the test-only RetopoEngine process stub; this configuration must not be shipped."
    )
endfunction()
