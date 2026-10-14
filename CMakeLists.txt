cmake_minimum_required(VERSION 3.16)
project(Stitching CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de build" FORCE)
endif()

# stitching : cv::detail::GraphCutSeamFinder ; features2d : SIFT ; calib3d : RANSAC ; imgcodecs : lecture des .tif
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs features2d calib3d stitching)
find_package(nlohmann_json 3 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

# Tout sauf les main → bibliothèque, réutilisable pour tester un algo seul.
# Les nouveaux .cpp sont pris en compte automatiquement.
file(GLOB_RECURSE LIB_SRC CONFIGURE_DEPENDS
     src/core/*.cpp src/steps/*.cpp src/algorithms/*.cpp src/app/*.cpp
     src/pretreatment_seamline/*.cpp src/positioning/*.cpp)
add_library(stitching_lib STATIC ${LIB_SRC})
target_include_directories(stitching_lib PUBLIC include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(stitching_lib PUBLIC ${OpenCV_LIBS} nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(stitching_lib PUBLIC /utf-8)
else()
  target_compile_options(stitching_lib PRIVATE -Wall -Wextra)
endif()

# Pipeline (squelette, pour plus tard)
add_executable(stitching src/main.cpp)
target_link_libraries(stitching PRIVATE stitching_lib)

# Positionnement SIFT + RANSAC puis coupe de graphe : tuiles .tif -> mosaïque, coutures, territoires
add_executable(seamline_graphcut src/main_graphcut.cpp)
target_link_libraries(seamline_graphcut PRIVATE stitching_lib)

# Outil de test : positions seules (SIFT + RANSAC), sans coupe de graphe. Pas de test automatique : pas de données dans le dépôt.
include(CTest)
if(BUILD_TESTING)
  add_executable(positioning_test tests/PositioningTest.cpp)
  target_link_libraries(positioning_test PRIVATE stitching_lib)
endif()

# Interface graphique (Qt 6), construite seulement si Qt est installé
find_package(Qt6 QUIET COMPONENTS Widgets Concurrent)
if(Qt6_FOUND)
  file(GLOB GUI_SRC CONFIGURE_DEPENDS gui/*.cpp)
  add_executable(stitching_gui ${GUI_SRC})
  target_link_libraries(stitching_gui PRIVATE stitching_lib Qt6::Widgets Qt6::Concurrent)
else()
  message(STATUS "Qt6 not found: stitching_gui will not be built")
endif()
