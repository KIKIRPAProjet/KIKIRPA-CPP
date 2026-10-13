cmake_minimum_required(VERSION 3.16)
project(Stitching CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources en UTF-8 (commentaires et messages en français) : MSVC suppose sinon la page de code du système
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

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

# Tests (ctest) : vérité exacte sur paires découpées + cas limites. Voir tests/test_correlation_phase.cpp
enable_testing()
add_executable(test_correlation_phase tests/test_correlation_phase.cpp)
target_link_libraries(test_correlation_phase PRIVATE stitching_lib)
add_test(NAME correlation_phase COMMAND test_correlation_phase)

# Seamline : images en gris, repli de la DP contrainte. Voir tests/test_seamline.cpp
add_executable(test_seamline tests/test_seamline.cpp)
target_link_libraries(test_seamline PRIVATE stitching_lib)
add_test(NAME seamline COMMAND test_seamline)

# Chaîne complète sur une paire (corrélation de phase → positions → seamline → fusion), hors pipeline.
# Sans argument : vérité exacte (ctest). Mode --paire : paire réelle. Voir tests/test_chaine_paire.cpp
add_executable(test_chaine_paire tests/test_chaine_paire.cpp)
target_link_libraries(test_chaine_paire PRIVATE stitching_lib)
add_test(NAME chaine_paire COMMAND test_chaine_paire)

# Interface graphique (Qt 6), construite seulement si Qt est installé
find_package(Qt6 QUIET COMPONENTS Widgets Concurrent)
if(Qt6_FOUND)
  file(GLOB GUI_SRC CONFIGURE_DEPENDS gui/*.cpp)
  add_executable(stitching_gui ${GUI_SRC})
  target_link_libraries(stitching_gui PRIVATE stitching_lib Qt6::Widgets Qt6::Concurrent)
else()
  message(STATUS "Qt6 not found: stitching_gui will not be built")
endif()
