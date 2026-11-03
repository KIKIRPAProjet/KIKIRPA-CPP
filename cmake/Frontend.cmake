file(GLOB_RECURSE frontend_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/frontend/*.cpp"
)

add_library(stitching_frontend STATIC ${frontend_sources})
target_compile_features(stitching_frontend PUBLIC cxx_std_17)
target_include_directories(stitching_frontend PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/frontend"
)
target_link_libraries(stitching_frontend PUBLIC
    kikirpa_backend
    Qt6::Widgets
    Qt6::Concurrent
)
