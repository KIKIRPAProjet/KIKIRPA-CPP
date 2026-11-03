# Deux exécutables : les deux fichiers définissent les mêmes fonctions d'aide (verifier…)
add_executable(pipeline_tests test_pipeline.cpp)
target_link_libraries(pipeline_tests PRIVATE kikirpa_backend GTest::gtest_main)

add_executable(reprise_tests test_reprise.cpp)
target_link_libraries(reprise_tests PRIVATE kikirpa_backend GTest::gtest_main)

add_executable(pyramidal_tiff_reader_tests test_pyramidal_tiff_reader.cpp)
target_link_libraries(pyramidal_tiff_reader_tests PRIVATE stitching_backend GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(pipeline_tests PROPERTIES LABELS "unit")
gtest_discover_tests(reprise_tests PROPERTIES LABELS "unit")
gtest_discover_tests(pyramidal_tiff_reader_tests PROPERTIES LABELS "unit")
