add_executable(correlation_phase_tests test_correlation_phase.cpp)

target_link_libraries(correlation_phase_tests PRIVATE kikirpa_backend GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(correlation_phase_tests PROPERTIES LABELS "unit")
