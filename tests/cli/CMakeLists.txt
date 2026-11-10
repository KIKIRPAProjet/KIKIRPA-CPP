add_test(
    NAME cli_pipeline_empty
    COMMAND $<TARGET_FILE:stitching> --pipeline "${CMAKE_CURRENT_SOURCE_DIR}/empty_pipeline.json"
)

add_test(
    NAME cli_pipeline_missing_path
    COMMAND "${CMAKE_COMMAND}"
        "-DSTITCHING_EXECUTABLE=$<TARGET_FILE:stitching>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_missing_pipeline.cmake"
)

add_test(NAME cli_default_frontend COMMAND $<TARGET_FILE:stitching>)

set_tests_properties(cli_pipeline_empty cli_default_frontend PROPERTIES LABELS "unit")
set_tests_properties(cli_pipeline_missing_path PROPERTIES
    LABELS "unit"
)
set_tests_properties(
    cli_pipeline_empty
    cli_pipeline_missing_path
    cli_default_frontend
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
set_tests_properties(cli_default_frontend PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;KIKIRPA_FRONTEND_STARTUP_TEST=1"
)
