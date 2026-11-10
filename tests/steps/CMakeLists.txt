add_executable(test_lightglue_step test_lightglue_step.cpp)
target_link_libraries(test_lightglue_step PRIVATE kikirpa_backend GTest::gtest_main)
kikirpa_copy_onnxruntime_dlls(test_lightglue_step)
include(GoogleTest)
gtest_discover_tests(test_lightglue_step PROPERTIES LABELS "unit")

# Test avec un vrai modèle. Par défaut, il est téléchargé pendant la configuration ;
# -DLIGHTGLUE_TEST_MODEL=<chemin> permet d'en utiliser un déjà présent.
set(LIGHTGLUE_TEST_MODEL "" CACHE FILEPATH "Modèle ONNX pour le test d'inférence LightGlue (téléchargé si vide)")
if(LIGHTGLUE_TEST_MODEL)
    if(NOT EXISTS "${LIGHTGLUE_TEST_MODEL}")
        message(FATAL_ERROR "LIGHTGLUE_TEST_MODEL does not exist: ${LIGHTGLUE_TEST_MODEL}")
    endif()
    set(lightglue_test_model "${LIGHTGLUE_TEST_MODEL}")
else()
    set(lightglue_test_model "${CMAKE_BINARY_DIR}/models/raco_aliked_lightglue_pipeline_k1024.onnx")
    file(DOWNLOAD
        https://github.com/fabio-sim/LightGlue-ONNX/releases/download/v3.0/raco_aliked_lightglue_pipeline_k1024.onnx
        "${lightglue_test_model}"
        EXPECTED_HASH SHA256=575e1746138204e19027cb37541cd840c7fc9f2c932b844e9685354a3913bda1
    )
endif()

add_executable(test_lightglue_model test_lightglue_model.cpp)
target_link_libraries(test_lightglue_model PRIVATE kikirpa_backend)
kikirpa_copy_onnxruntime_dlls(test_lightglue_model)
add_test(NAME lightglue_model COMMAND test_lightglue_model "${lightglue_test_model}")
set_tests_properties(lightglue_model PROPERTIES LABELS "integration")

add_executable(test_absolute_pos_step test_absolute_pos_step.cpp)
kikirpa_copy_onnxruntime_dlls(test_absolute_pos_step)
target_link_libraries(test_absolute_pos_step PRIVATE kikirpa_backend GTest::gtest_main)
gtest_discover_tests(test_absolute_pos_step PROPERTIES LABELS "unit")

add_executable(test_sift_step test_sift_step.cpp)
kikirpa_copy_onnxruntime_dlls(test_sift_step)
target_link_libraries(test_sift_step PRIVATE kikirpa_backend GTest::gtest_main)
gtest_discover_tests(test_sift_step PROPERTIES LABELS "unit")

add_executable(test_mosaic_crop_step test_mosaic_crop_step.cpp)

target_link_libraries(test_mosaic_crop_step PRIVATE
    kikirpa_backend
    GTest::gtest_main
)

kikirpa_copy_onnxruntime_dlls(test_mosaic_crop_step)

gtest_discover_tests(test_mosaic_crop_step
    PROPERTIES LABELS "unit"
)
add_executable(test_vote_2d_step test_vote_2d_step.cpp)
kikirpa_copy_onnxruntime_dlls(test_vote_2d_step)
target_link_libraries(test_vote_2d_step PRIVATE kikirpa_backend GTest::gtest_main)
gtest_discover_tests(test_vote_2d_step PROPERTIES LABELS "unit")
