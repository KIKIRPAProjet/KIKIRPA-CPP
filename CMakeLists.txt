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

# Les nouveaux .cpp sont pris en compte automatiquement.
file(GLOB_RECURSE BACKEND_SRC CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/Backend/*.cpp")
add_library(stitching_backend STATIC ${BACKEND_SRC})
target_include_directories(stitching_backend PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/Backend")
target_link_libraries(stitching_backend PUBLIC ${OpenCV_LIBS} nlohmann_json::nlohmann_json)

# Ligne de commande
add_executable(stitching src/main.cpp)
target_link_libraries(stitching PRIVATE stitching_backend)

# Test du backend : charge un script pipeline JSON et l'exécute
add_executable(stitching_test src/main_test.cpp)
target_link_libraries(stitching_test PRIVATE stitching_backend)
# Racine du projet : pipeline.json et images/ sont trouvés quel que soit le dossier de lancement
target_compile_definitions(stitching_test PRIVATE PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Générateur de pipeline.json : options saisies en console (--list pour voir les options disponibles)
add_executable(generate_pipeline src/main_generate.cpp)
target_link_libraries(generate_pipeline PRIVATE stitching_backend)
target_compile_definitions(generate_pipeline PRIVATE PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Interface graphique (Qt 6), construite seulement si Qt est installé
find_package(Qt6 QUIET COMPONENTS Widgets Concurrent)
if(Qt6_FOUND)
  file(GLOB_RECURSE FRONTEND_SRC CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/src/Frontend/*.cpp")
  list(FILTER FRONTEND_SRC EXCLUDE REGEX "/ToDelete\\.cpp$")
  if(FRONTEND_SRC)
    add_executable(stitching_frontend ${FRONTEND_SRC})
    target_link_libraries(stitching_frontend PRIVATE stitching_backend Qt6::Widgets Qt6::Concurrent)
  else()
    message(STATUS "No frontend sources found: stitching_frontend will not be built")
  endif()
else()
  message(STATUS "Qt6 not found: stitching_frontend will not be built")
endif()
