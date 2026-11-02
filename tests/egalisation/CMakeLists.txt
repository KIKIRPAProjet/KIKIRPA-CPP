add_executable(egalisation_tests test_egalisation.cpp)

target_link_libraries(egalisation_tests PRIVATE stitching_backend GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(egalisation_tests PROPERTIES LABELS "unit")
