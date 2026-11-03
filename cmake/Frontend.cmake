# Sources du frontend

# Récupère récursivement tous les fichiers .cpp du répertoire src/frontend.
# CONFIGURE_DEPENDS demande à CMake de vérifier si la liste des fichiers
# a changé et de relancer la configuration si nécessaire.
file(GLOB_RECURSE frontend_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/frontend/*.cpp"
)

# Création de la bibliothèque frontend

# Crée une bibliothèque statique regroupant les fonctionnalités du frontend.
# Elle contient notamment les composants de l'interface graphique.
add_library(kikirpa_frontend STATIC ${frontend_sources})

# Impose la norme C++17 aux cibles qui utilisent cette bibliothèque.
# PUBLIC permet également de transmettre cette exigence aux cibles dépendantes.
target_compile_features(kikirpa_frontend PUBLIC cxx_std_17)

# Définit les répertoires contenant les fichiers d'en-tête accessibles
# aux cibles qui utilisent le frontend.
#
# - include : répertoire principal des en-têtes du projet.
# - include/frontend : en-têtes spécifiques à l'interface graphique.
target_include_directories(kikirpa_frontend PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/frontend"
)

# Dépendances du frontend

# Lie les bibliothèques nécessaires au fonctionnement du frontend.
#
# - kikirpa_backend : fonctionnalités métier et traitements du backend.
# - Qt6::Widgets : composants graphiques de l'interface utilisateur.
# - Qt6::Concurrent : prise en charge des traitements concurrents et asynchrones.
#
# PUBLIC signifie que ces dépendances sont également propagées aux cibles
# qui utilisent kikirpa_frontend.
target_link_libraries(kikirpa_frontend PUBLIC
    kikirpa_backend
    Qt6::Widgets
    Qt6::Concurrent
)
