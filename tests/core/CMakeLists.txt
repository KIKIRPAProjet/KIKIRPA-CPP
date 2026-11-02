# Deux exécutables : les deux fichiers définissent les mêmes fonctions d'aide (verifier…)
add_executable(pipeline_tests test_pipeline.cpp)
target_link_libraries(pipeline_tests PRIVATE stitching_backend GTest::gtest_main)

add_executable(reprise_tests test_reprise.cpp)
target_link_libraries(reprise_tests PRIVATE stitching_backend GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(pipeline_tests PROPERTIES LABELS "unit")
gtest_discover_tests(reprise_tests PROPERTIES LABELS "unit")
