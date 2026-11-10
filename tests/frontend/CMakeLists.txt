add_executable(test_frontend_project_setup test_project_setup.cpp)
target_link_libraries(test_frontend_project_setup PRIVATE kikirpa_frontend GTest::gtest_main)
target_compile_features(test_frontend_project_setup PRIVATE cxx_std_17)
include(GoogleTest)
gtest_discover_tests(test_frontend_project_setup)
# Démonstration manuelle de la superposition des couches
add_executable(demo_image_layers
    demo_image_layers.cpp
)

target_include_directories(demo_image_layers PRIVATE
    "${PROJECT_SOURCE_DIR}/include/frontend"
)

target_link_libraries(demo_image_layers PRIVATE
    kikirpa_frontend
    Qt6::Widgets
)