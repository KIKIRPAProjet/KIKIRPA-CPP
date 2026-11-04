add_executable(phase_correlation_tests test_phase_correlation.cpp)

target_link_libraries(phase_correlation_tests PRIVATE kikirpa_backend GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(phase_correlation_tests PROPERTIES LABELS "unit")
