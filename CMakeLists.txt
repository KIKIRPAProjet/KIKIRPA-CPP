cmake_minimum_required(VERSION 3.16)
project(Stitching CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Backend ---
find_package(OpenCV REQUIRED)
find_package(nlohmann_json 3 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

file(GLOB_RECURSE BACKEND_SRC CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/Backend/*.cpp")
add_library(stitching_backend STATIC ${BACKEND_SRC})
target_include_directories(stitching_backend PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/Backend")
target_link_libraries(stitching_backend PUBLIC ${OpenCV_LIBS} nlohmann_json::nlohmann_json)

# Algorithmes (LightGlue/ONNX Runtime, tests) : voir cmake/algorithms.cmake
include(cmake/algorithms.cmake)


# --- Frontend ---
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
file(GLOB_RECURSE FRONTEND_SRC CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Frontend/*.cpp")
add_library(stitching_frontend STATIC ${FRONTEND_SRC})
target_include_directories(stitching_frontend PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/Frontend")
target_link_libraries(stitching_frontend PUBLIC stitching_backend Qt6::Widgets Qt6::Concurrent)

# Ligne de commande
add_executable(stitching src/main.cpp)
target_link_libraries(stitching PRIVATE stitching_backend)