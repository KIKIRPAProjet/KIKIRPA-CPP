cmake_minimum_required(VERSION 3.16)
project(Stitching CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(nlohmann_json 3 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

# Tout sauf les main → bibliothèque, réutilisable pour tester un algo seul.
# Les nouveaux .cpp sont pris en compte automatiquement.
file(GLOB_RECURSE LIB_SRC CONFIGURE_DEPENDS
     src/core/*.cpp src/steps/*.cpp src/algorithms/*.cpp src/app/*.cpp)
add_library(stitching_lib STATIC ${LIB_SRC})
target_include_directories(stitching_lib PUBLIC include)
target_link_libraries(stitching_lib PUBLIC ${OpenCV_LIBS} nlohmann_json::nlohmann_json)

# Ligne de commande
add_executable(stitching src/main.cpp)
target_link_libraries(stitching PRIVATE stitching_lib)

# Interface graphique (Qt 6), construite seulement si Qt est installé
find_package(Qt6 QUIET COMPONENTS Widgets Concurrent)
if(Qt6_FOUND)
  file(GLOB GUI_SRC CONFIGURE_DEPENDS gui/*.cpp)
  add_executable(stitching_gui ${GUI_SRC})
  target_link_libraries(stitching_gui PRIVATE stitching_lib Qt6::Widgets Qt6::Concurrent)
else()
  message(STATUS "Qt6 not found: stitching_gui will not be built")
endif()
