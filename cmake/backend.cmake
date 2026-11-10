# Sources: backend

# Récupère récursivement tous les fichiers .cpp du répertoire src/backend.
# CONFIGURE_DEPENDS demande à CMake de vérifier si la liste des fichiers
# a changé et de relancer la configuration si nécessaire.
file(GLOB_RECURSE backend_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/backend/*.cpp"
)

# Création de la bibliothèque backend

# Crée une bibliothèque statique contenant les fonctionnalités du backend.
add_library(kikirpa_backend STATIC ${backend_sources})

# Impose la norme C++17 aux cibles qui utilisent cette bibliothèque.
# PUBLIC permet également de transmettre cette exigence aux cibles dépendantes.
target_compile_features(kikirpa_backend PUBLIC cxx_std_17)

# Définit les répertoires d'en-têtes accessibles aux cibles qui utilisent
# le backend.
#
# - include : répertoire principal des en-têtes du projet.
# - include/backend : en-têtes spécifiques au backend.
# - OpenCV_INCLUDE_DIRS : en-têtes de la bibliothèque OpenCV.
target_include_directories(kikirpa_backend PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/backend"
    ${OpenCV_INCLUDE_DIRS}
)

# Lie les dépendances nécessaires au fonctionnement du backend.
#
# PUBLIC signifie que ces dépendances sont également propagées aux cibles
# qui utilisent kikirpa_backend.
#
# - OpenCV_LIBS : bibliothèques OpenCV nécessaires aux traitements d'images.
# - nlohmann_json : bibliothèque de manipulation JSON.
# - TIFF::TIFF : bibliothèque de lecture et d'écriture TIFF.
target_link_libraries(kikirpa_backend PUBLIC
    ${OpenCV_LIBS}
    nlohmann_json::nlohmann_json
    TIFF::TIFF
)

if(WIN32)
    target_link_libraries(kikirpa_backend PUBLIC shell32 ole32)
endif()

# Ajoute les options de compilation spécifiques au compilateur.
#
# MSVC : /utf-8 force l'interprétation des fichiers sources et des chaînes
# de caractères en UTF-8, évitant notamment les problèmes d'encodage
# sous Windows.
target_compile_options(kikirpa_backend PUBLIC
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

# Configuration spécifique à LightGlue

# Rend les en-têtes d'ONNX Runtime accessibles au backend
# et aux cibles qui en dépendent.
target_include_directories(kikirpa_backend PUBLIC
    "${ONNXRUNTIME_INCLUDE_DIR}"
)

# Lie ONNX Runtime et le module dnn d'OpenCV au backend.
target_link_libraries(kikirpa_backend PUBLIC
    "${ONNXRUNTIME_LIB}" opencv_dnn
)

# Définit le chemin racine du projet pour le code LightGlue.
# Cette définition est privée : elle n'est pas propagée aux autres cibles.
target_compile_definitions(kikirpa_backend PRIVATE
    STITCHING_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
)

# Sous Windows, System32 contient souvent un onnxruntime.dll plus ancien, chargé avant le PATH :
# l'exécutable plante au premier appel. Les DLL copiées à côté de lui ont priorité.
get_filename_component(onnxruntime_library_directory "${ONNXRUNTIME_LIB}" DIRECTORY)
file(GLOB onnxruntime_dlls CONFIGURE_DEPENDS "${onnxruntime_library_directory}/onnxruntime*.dll")

# Copie les DLL d'ONNX Runtime à côté d'un exécutable lié au backend, une seule fois par dossier :
# deux copies simultanées vers le même fichier échouent. Sans effet hors de Windows.
function(kikirpa_copy_onnxruntime_dlls target)
    if(WIN32)
        string(MAKE_C_IDENTIFIER "${CMAKE_CURRENT_BINARY_DIR}" output_directory_id)
        set(copy_target "onnxruntime_dlls_${output_directory_id}")
        if(NOT TARGET ${copy_target})
            add_custom_target(${copy_target}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${onnxruntime_dlls} "${CMAKE_CURRENT_BINARY_DIR}"
            )
        endif()
        add_dependencies(${target} ${copy_target})
    endif()
endfunction()
