add_executable(phase_correlation_tests test_phase_correlation.cpp)

kikirpa_copy_onnxruntime_dlls(phase_correlation_tests)
target_link_libraries(phase_correlation_tests PRIVATE kikirpa_backend GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(phase_correlation_tests PROPERTIES LABELS "unit")

# Étude de l'indicateur de confiance : compilée seulement, pas un test (ctest ne la lance pas)
#   etude_confiance [resultats.csv]   (voir docs/algorithms/stitching/phase_correlation/EtudeConfiance.md)
add_executable(etude_confiance etude_confiance.cpp)
target_link_libraries(etude_confiance PRIVATE kikirpa_backend)
