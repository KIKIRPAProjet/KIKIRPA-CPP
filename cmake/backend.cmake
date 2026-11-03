# Sources: backend

# Récupère récursivement tous les fichiers .cpp du répertoire src/backend.
# CONFIGURE_DEPENDS demande à CMake de vérifier si la liste des fichiers
# a changé et de relancer la configuration si nécessaire.
file(GLOB_RECURSE backend_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/backend/*.cpp"
)

# Isole les sources de l'algorithme LightGlue.
# Ces sources ne seront intégrées au backend que si toutes les dépendances
# nécessaires à LightGlue sont disponibles.
set(lightglue_sources ${backend_sources})

# Ne conserve dans lightglue_sources que les fichiers situés dans
# le répertoire dédié à l'implémentation LightGlue.
list(FILTER lightglue_sources INCLUDE REGEX "/algorithms/stitching/lightglue/")

# Retire les sources LightGlue de la liste principale.
# Elles seront éventuellement réintégrées plus bas, après vérification
# de la disponibilité des dépendances optionnelles.
list(REMOVE_ITEM backend_sources ${lightglue_sources})

# Recherche d'ONNX Runtime

# Définit le répertoire d'installation d'ONNX Runtime.
# Cette variable peut être renseignée lors de la configuration CMake :
# -DONNXRUNTIME_ROOT=<chemin>
#
# Elle est vide par défaut : ONNX Runtime reste une dépendance optionnelle.
set(ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime installation directory")

# Recherche le fichier d'en-tête principal d'ONNX Runtime.
# La recherche utilise ONNXRUNTIME_ROOT comme chemin indicatif,
# puis les chemins standards et les suffixes indiqués.
find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS "${ONNXRUNTIME_ROOT}/include"
    PATH_SUFFIXES onnxruntime include
)

# Recherche la bibliothèque binaire d'ONNX Runtime.
find_library(ONNXRUNTIME_LIB
    NAMES onnxruntime
    HINTS "${ONNXRUNTIME_ROOT}/lib"
)

# Activation conditionnelle de LightGlue

# LightGlue nécessite :
# - Les fichiers d'en-tête d'ONNX Runtime.
# - La bibliothèque ONNX Runtime.
# - Le module dnn d'OpenCV.
#
# Si toutes ces conditions sont remplies, ses sources sont ajoutées
# au backend. Dans le cas contraire, LightGlue est désactivé sans empêcher
# la compilation du reste du projet.
if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB AND TARGET opencv_dnn)

    # Réintègre les sources LightGlue dans la compilation du backend.
    list(APPEND backend_sources ${lightglue_sources})

    # Mémorise l'état de disponibilité de LightGlue pour les étapes suivantes.
    set(lightglue_enabled TRUE)

else()
    set(lightglue_enabled FALSE)
endif()

# Création de la bibliothèque backend

# Crée une bibliothèque statique contenant les fonctionnalités du backend.
# Les sources LightGlue ne sont présentes que si ses dépendances optionnelles
# ont été détectées précédemment.
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

# Ajoute les options de compilation spécifiques au compilateur.
#
# MSVC : /utf-8 force l'interprétation des fichiers sources et des chaînes
# de caractères en UTF-8, évitant notamment les problèmes d'encodage
# sous Windows.
target_compile_options(kikirpa_backend PUBLIC
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

# Configuration spécifique à LightGlue

if(lightglue_enabled)

    # Rend les en-têtes d'ONNX Runtime accessibles au backend
    # et aux cibles qui en dépendent.
    target_include_directories(kikirpa_backend PUBLIC
        "${ONNXRUNTIME_INCLUDE_DIR}"
    )

    # Lie la bibliothèque ONNX Runtime au backend.
    target_link_libraries(kikirpa_backend PUBLIC
        "${ONNXRUNTIME_LIB}"
    )

    # Définit le chemin racine du projet pour le code LightGlue.
    # Cette définition est privée : elle n'est pas propagée aux autres cibles.
    target_compile_definitions(kikirpa_backend PRIVATE
        STITCHING_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
    )

    message(STATUS "ONNX Runtime found: LightGlue enabled")

elseif(NOT TARGET opencv_dnn)

    # Le module dnn d'OpenCV est absent :
    # LightGlue ne peut donc pas être compilé.
    message(STATUS "OpenCV dnn not found: LightGlue disabled")

else()

    # OpenCV dnn est disponible, mais ONNX Runtime est absent
    # ou n'a pas été correctement détecté.
    message(STATUS "ONNX Runtime not found: LightGlue disabled (set ONNXRUNTIME_ROOT)")

endif()
