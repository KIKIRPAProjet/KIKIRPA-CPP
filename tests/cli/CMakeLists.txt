# tests/cli/CMakeLists.txt
add_executable(cli_pipeline_tests test_pipeline_2x2.cpp)
kikirpa_copy_onnxruntime_dlls(cli_pipeline_tests)
target_link_libraries(cli_pipeline_tests PRIVATE kikirpa_backend GTest::gtest_main)
target_compile_definitions(cli_pipeline_tests PRIVATE STITCHING_EXECUTABLE="$<TARGET_FILE:stitching>")
add_dependencies(cli_pipeline_tests stitching)

# Tests de l'exécutable `stitching` lancé comme un utilisateur : options, codes de sortie, reprise.
add_executable(cli_tests test_command_line.cpp)
kikirpa_copy_onnxruntime_dlls(cli_tests)
# kikirpa_backend pour OpenCV et nlohmann_json (génération des tuiles et des scripts), comme les autres tests.
target_link_libraries(cli_tests PRIVATE kikirpa_backend GTest::gtest_main)
target_compile_definitions(cli_tests PRIVATE STITCHING_EXECUTABLE="$<TARGET_FILE:stitching>")
add_dependencies(cli_tests stitching)

include(GoogleTest)
gtest_discover_tests(cli_pipeline_tests PROPERTIES LABELS "integration")
gtest_discover_tests(cli_tests PROPERTIES LABELS "unit")

# Sans argument, `stitching` ouvre l'interface graphique ; la variable d'environnement la referme aussitôt.
add_test(NAME cli_default_frontend COMMAND $<TARGET_FILE:stitching>)
set_tests_properties(cli_default_frontend PROPERTIES
    LABELS "unit"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;KIKIRPA_FRONTEND_STARTUP_TEST=1"
)
