add_executable(cli_pipeline_tests test_pipeline_2x2.cpp)
kikirpa_copy_onnxruntime_dlls(cli_pipeline_tests)
target_link_libraries(cli_pipeline_tests PRIVATE kikirpa_backend GTest::gtest_main)
target_compile_definitions(cli_pipeline_tests PRIVATE STITCHING_EXECUTABLE="$<TARGET_FILE:stitching>")
add_dependencies(cli_pipeline_tests stitching)

include(GoogleTest)
gtest_discover_tests(cli_pipeline_tests PROPERTIES LABELS "integration")
