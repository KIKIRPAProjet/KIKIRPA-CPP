add_test(
    NAME cli_pipeline_empty
    COMMAND $<TARGET_FILE:stitching> --pipeline "${CMAKE_CURRENT_SOURCE_DIR}/empty_pipeline.json"
)

add_test(
    NAME cli_pipeline_missing_path
    COMMAND $<TARGET_FILE:stitching> --pipeline
)

add_test(NAME cli_default_frontend COMMAND $<TARGET_FILE:stitching>)

set_tests_properties(cli_pipeline_empty cli_default_frontend PROPERTIES LABELS "unit")
set_tests_properties(cli_pipeline_missing_path PROPERTIES
    LABELS "unit"
    WILL_FAIL TRUE
    PASS_REGULAR_EXPRESSION "Usage: stitching --pipeline <script.json>"
)
