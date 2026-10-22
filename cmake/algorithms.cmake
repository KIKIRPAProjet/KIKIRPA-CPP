# Algorithmes (src/Backend/algorithms) : réglages propres aux algorithmes.
# Inclus par le CMakeLists.txt racine, APRÈS la création de stitching_backend :
#     include(cmake/algorithms.cmake)
# Les .cpp des algorithmes sont déjà compilés dans stitching_backend par le GLOB de src/Backend/.

# Sources en UTF-8 (commentaires et messages en français) : MSVC suppose sinon la page de code du système
target_compile_options(stitching_backend PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# LightGlue a besoin d'ONNX Runtime et du module dnn d'OpenCV. S'il en manque un, LightGlue n'est pas
# compilé ; les autres algorithmes le sont normalement.
# ONNX Runtime installé ailleurs que dans les chemins standard : cmake -DONNXRUNTIME_ROOT=<dossier d'ONNX Runtime>
set(ONNXRUNTIME_ROOT "" CACHE PATH "Dossier d'installation d'ONNX Runtime (contient include/ et lib/)")
find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h
          HINTS "${ONNXRUNTIME_ROOT}/include" PATH_SUFFIXES onnxruntime include)
find_library(ONNXRUNTIME_LIB NAMES onnxruntime HINTS "${ONNXRUNTIME_ROOT}/lib")
if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB AND TARGET opencv_dnn)
  set(STITCHING_LIGHTGLUE_AVAILABLE TRUE)
  target_compile_definitions(stitching_backend PRIVATE STITCHING_HAS_LIGHTGLUE=1)
  target_include_directories(stitching_backend PUBLIC ${ONNXRUNTIME_INCLUDE_DIR})
  target_link_libraries(stitching_backend PUBLIC ${ONNXRUNTIME_LIB})
  # Utilisé par LightGlueMatcher.cpp pour retrouver le modèle ONNX à partir de la racine du projet
  target_compile_definitions(stitching_backend PRIVATE STITCHING_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
  message(STATUS "ONNX Runtime trouvé : LightGlue sera compilé")
else()
  set(STITCHING_LIGHTGLUE_AVAILABLE FALSE)
  get_target_property(_sources_backend stitching_backend SOURCES)
  list(FILTER _sources_backend INCLUDE REGEX "/algorithms/stitching/lightglue/")
  set_source_files_properties(${_sources_backend} PROPERTIES HEADER_FILE_ONLY ON)
  if(NOT TARGET opencv_dnn)
    message(STATUS "Module dnn d'OpenCV absent : LightGlue ne sera pas compilé")
  else()
    message(STATUS "ONNX Runtime introuvable (option ONNXRUNTIME_ROOT) : LightGlue ne sera pas compilé")
  endif()
endif()

# Tests des algorithmes : ctest après la compilation
include(CTest)
if(BUILD_TESTING)
  add_executable(stitching_lightglue "${CMAKE_CURRENT_SOURCE_DIR}/tests/steps/run_lightglue_pipeline.cpp")
  target_link_libraries(stitching_lightglue PRIVATE stitching_backend)
  add_executable(test_lightglue_step "${CMAKE_CURRENT_SOURCE_DIR}/tests/steps/test_lightglue_step.cpp")
  target_link_libraries(test_lightglue_step PRIVATE stitching_backend)
  add_test(NAME lightglue_step COMMAND test_lightglue_step)
  set(LIGHTGLUE_TEST_MODEL "" CACHE FILEPATH "Modèle ONNX pour le test d'inférence LightGlue")
  if(STITCHING_LIGHTGLUE_AVAILABLE)
    add_executable(test_lightglue_model "${CMAKE_CURRENT_SOURCE_DIR}/tests/steps/test_lightglue_model.cpp")
    target_link_libraries(test_lightglue_model PRIVATE stitching_backend)
    if(LIGHTGLUE_TEST_MODEL)
      if(NOT EXISTS "${LIGHTGLUE_TEST_MODEL}")
        message(FATAL_ERROR "LIGHTGLUE_TEST_MODEL does not exist: ${LIGHTGLUE_TEST_MODEL}")
      endif()
      add_test(NAME lightglue_model COMMAND test_lightglue_model "${LIGHTGLUE_TEST_MODEL}")
    endif()
  elseif(LIGHTGLUE_TEST_MODEL)
    message(FATAL_ERROR "LIGHTGLUE_TEST_MODEL requires ONNX Runtime and OpenCV dnn")
  endif()
  set(_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/algorithms")

  # Sans données externes : lancés par ctest
  add_executable(test_correlation_phase "${_tests}/stitching/correlation_phase/test_correlation_phase.cpp")
  add_executable(test_seamline "${_tests}/seamline/dp_dijkstra/test_seamline.cpp")
  add_executable(test_chaine_paire "${_tests}/integration/test_chaine_paire.cpp")
  add_test(NAME correlation_phase COMMAND test_correlation_phase)
  add_test(NAME seamline COMMAND test_seamline)
  add_test(NAME chaine_paire COMMAND test_chaine_paire)

  # Avec des images (non fournies dans le dépôt) : compilés seulement, à lancer à la main
  #   sift_matcher_test <dossier des images> [recouvrement]   (voir tests/algorithms/stitching/sift/data/README.md)
  #   positioning_test [--chevauchement=0.3] <tuile1_rLL_cCC.tif> [tuile2 ...]
  add_executable(sift_matcher_test "${_tests}/stitching/sift/SiftMatcherTest.cpp")
  add_executable(positioning_test "${_tests}/stitching/positioning/PositioningTest.cpp")

  foreach(_cible test_correlation_phase test_seamline test_chaine_paire sift_matcher_test positioning_test)
    target_link_libraries(${_cible} PRIVATE stitching_backend)
  endforeach()
endif()
