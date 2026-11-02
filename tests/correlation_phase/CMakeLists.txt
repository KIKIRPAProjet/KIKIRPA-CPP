add_executable(correlation_phase_tests test_correlation_phase.cpp)

target_link_libraries(correlation_phase_tests PRIVATE stitching_backend GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(correlation_phase_tests PROPERTIES LABELS "unit")

# Étude de l'indicateur de confiance : compilée seulement, pas un test (ctest ne la lance pas)
#   etude_confiance [resultats.csv]   (voir docs/algorithms/stitching/correlation_phase/EtudeConfiance.md)
add_executable(etude_confiance etude_confiance.cpp)
target_link_libraries(etude_confiance PRIVATE stitching_backend)
