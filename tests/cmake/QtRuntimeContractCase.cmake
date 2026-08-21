if(NOT DEFINED RETOPRIME_SOURCE_DIR)
    message(FATAL_ERROR "RETOPRIME_SOURCE_DIR is required")
endif()

include("${RETOPRIME_SOURCE_DIR}/cmake/QtRuntimeContract.cmake")
retoprime_enforce_qt_runtime_contract(FALSE FALSE)
