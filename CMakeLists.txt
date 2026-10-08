cmake_minimum_required(VERSION 3.16)
project(Stitching CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

# ONNX Runtime -> ajout pour LightGlue
find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h PATH_SUFFIXES onnxruntime)
find_library(ONNXRUNTIME_LIB NAMES onnxruntime)

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

# Ajout pour LightGlue
target_include_directories(stitching_lib PUBLIC include ${ONNXRUNTIME_INCLUDE_DIR})
target_compile_definitions(stitching_lib PRIVATE
    STITCHING_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(stitching_lib PUBLIC ${OpenCV_LIBS} nlohmann_json::nlohmann_json ${ONNXRUNTIME_LIB})

# Ligne de commande
add_executable(stitching src/main.cpp)
target_link_libraries(stitching PRIVATE stitching_lib)

# Interface graphique (Qt 6), construite seulement si Qt est installé
list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qtbase") # Si problème avec le chemin et détection de Qt, utiliser brew --prefix qtbase si installé via homebrew sur mac
find_package(Qt6 QUIET COMPONENTS Widgets Concurrent)
if(Qt6_FOUND)
  file(GLOB GUI_SRC CONFIGURE_DEPENDS gui/*.cpp)
  add_executable(stitching_gui ${GUI_SRC})
  target_link_libraries(stitching_gui PRIVATE stitching_lib Qt6::Widgets Qt6::Concurrent)

  # Homebrew installs Qt's macOS platform plugin below qtbase/share.
  # Copy it next to the executable so it is found when launching from build/.
  set(QT_PLATFORM_PLUGIN_DIR "${Qt6_DIR}/../../../share/qt/plugins/platforms")
  add_custom_command(TARGET stitching_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:stitching_gui>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_PLATFORM_PLUGIN_DIR}/libqcocoa.dylib"
            "$<TARGET_FILE_DIR:stitching_gui>/platforms/libqcocoa.dylib")
else()
  message(STATUS "Qt6 not found: stitching_gui will not be built")
endif()
