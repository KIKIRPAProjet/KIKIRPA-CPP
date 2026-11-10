add_executable(seam_tests
    test_seam_finder.cpp
    test_seam_finder_pipeline.cpp
    test_cost_map.cpp
    test_cost_catalog.cpp
    test_dynamic_programming.cpp
    test_dijkstra.cpp
    test_solver.cpp
    test_junctions.cpp
)

target_link_libraries(seam_tests PRIVATE kikirpa_backend GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(seam_tests PROPERTIES LABELS "unit")

# Tests de la coupe de graphe (GraphCutClass et son step)
add_subdirectory(graphcut)