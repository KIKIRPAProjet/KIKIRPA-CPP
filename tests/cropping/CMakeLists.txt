# tests/cropping/CMakeLists.txt

add_executable(mosaic_crop_tests
    test_mosaic_crop.cpp
)

target_link_libraries(mosaic_crop_tests PRIVATE
    kikirpa_backend
    GTest::gtest_main
)

kikirpa_copy_onnxruntime_dlls(mosaic_crop_tests)

include(GoogleTest)

gtest_discover_tests(mosaic_crop_tests
    PROPERTIES LABELS "unit"
)