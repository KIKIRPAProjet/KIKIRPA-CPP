cmake_minimum_required(VERSION 3.16)
project(Stitching CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Backend ---
find_package(OpenCV REQUIRED)
# libtiff : écriture des TIFF tuilés pyramidaux (core/PyramidalTiffWriter). Sous Windows, l'installer avec
# vcpkg et configurer avec le preset "windows-msvc" (voir docs/BUILD.md).
find_package(TIFF REQUIRED)
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
        ${OpenCV_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/Backend")
target_link_libraries(stitching_backend PUBLIC
        ${OpenCV_LIBS}
        nlohmann_json::nlohmann_json
        TIFF::TIFF)

# Algorithmes (LightGlue/ONNX Runtime, tests) : voir cmake/algorithms.cmake
include(cmake/algorithms.cmake)


# --- Frontend ---
# Facultatif : sans Qt 6, la GUI n'est pas construite, le backend, la CLI et les tests le sont.
find_package(Qt6 QUIET COMPONENTS Widgets Concurrent)
if(Qt6_FOUND)
  file(GLOB_RECURSE FRONTEND_SRC CONFIGURE_DEPENDS
          "${CMAKE_CURRENT_SOURCE_DIR}/src/Frontend/*.cpp")
  list(FILTER FRONTEND_SRC EXCLUDE REGEX "/ToDelete\\.cpp$")
  add_library(stitching_frontend STATIC ${FRONTEND_SRC})
  target_include_directories(stitching_frontend PUBLIC
          "${CMAKE_CURRENT_SOURCE_DIR}/include"
          "${CMAKE_CURRENT_SOURCE_DIR}/include/Frontend")
  target_link_libraries(stitching_frontend PUBLIC stitching_backend Qt6::Widgets Qt6::Concurrent)
else()
  message(STATUS "Qt 6 introuvable : la GUI (stitching_frontend) ne sera pas construite")
endif()

# Ligne de commande
add_executable(stitching src/main.cpp)
target_link_libraries(stitching PRIVATE stitching_backend)

# Lance un pipeline à partir d'un script JSON : stitching_run <script.json> (exemple : examples/pipeline.json)
add_executable(stitching_run src/run_pipeline.cpp)
target_link_libraries(stitching_run PRIVATE stitching_backend)