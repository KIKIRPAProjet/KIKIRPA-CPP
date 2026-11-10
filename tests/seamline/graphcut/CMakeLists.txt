add_executable(graphcut_tests test_graph_cut.cpp)
target_link_libraries(graphcut_tests PRIVATE kikirpa_backend GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(graphcut_tests PROPERTIES LABELS "unit")