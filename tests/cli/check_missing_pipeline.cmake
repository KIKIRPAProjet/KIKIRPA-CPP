if(NOT DEFINED STITCHING_EXECUTABLE)
    message(FATAL_ERROR "STITCHING_EXECUTABLE must be set")
endif()

set(missing_pipeline "${CMAKE_CURRENT_BINARY_DIR}/missing_pipeline.json")
if(EXISTS "${missing_pipeline}")
    message(FATAL_ERROR "Expected pipeline file not to exist: ${missing_pipeline}")
endif()

execute_process(
    COMMAND "${STITCHING_EXECUTABLE}" --pipeline "${missing_pipeline}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(result EQUAL 0)
    message(FATAL_ERROR "Expected pipeline execution to fail for a missing file")
endif()

if(NOT error MATCHES "Pipeline error: cannot open")
    message(FATAL_ERROR "Expected missing-file error, got: ${output}${error}")
endif()
