# Sources du backend. LightGlue est ajouté uniquement si ses dépendances optionnelles sont disponibles.
file(GLOB_RECURSE backend_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/backend/*.cpp"
)
set(lightglue_sources ${backend_sources})
list(FILTER lightglue_sources INCLUDE REGEX "/algorithms/stitching/lightglue/")
list(REMOVE_ITEM backend_sources ${lightglue_sources})

set(ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime installation directory")
find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS "${ONNXRUNTIME_ROOT}/include"
    PATH_SUFFIXES onnxruntime include
)
find_library(ONNXRUNTIME_LIB
    NAMES onnxruntime
    HINTS "${ONNXRUNTIME_ROOT}/lib"
)

if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB AND TARGET opencv_dnn)
    list(APPEND backend_sources ${lightglue_sources})
    set(lightglue_enabled TRUE)
else()
    set(lightglue_enabled FALSE)
endif()

add_library(stitching_backend STATIC ${backend_sources})
target_compile_features(stitching_backend PUBLIC cxx_std_17)
target_include_directories(stitching_backend PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/backend"
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(stitching_backend PUBLIC
    ${OpenCV_LIBS}
    nlohmann_json::nlohmann_json
    TIFF::TIFF
)
target_compile_options(stitching_backend PUBLIC
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

if(lightglue_enabled)
    target_include_directories(stitching_backend PUBLIC "${ONNXRUNTIME_INCLUDE_DIR}")
    target_link_libraries(stitching_backend PUBLIC "${ONNXRUNTIME_LIB}" opencv_dnn)
    target_compile_definitions(stitching_backend PRIVATE
        STITCHING_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
    )
    message(STATUS "ONNX Runtime found: LightGlue enabled")
elseif(NOT TARGET opencv_dnn)
    message(STATUS "OpenCV dnn not found: LightGlue disabled")
else()
    message(STATUS "ONNX Runtime not found: LightGlue disabled (set ONNXRUNTIME_ROOT)")
endif()
