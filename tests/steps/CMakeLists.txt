add_executable(test_sift_step test_sift_step.cpp)
target_link_libraries(test_sift_step PRIVATE kikirpa_backend GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(test_sift_step PROPERTIES LABELS "unit")
