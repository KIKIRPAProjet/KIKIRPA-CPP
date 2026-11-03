# Point d'entrée principal de la configuration CMake du projet.
# Définit la version minimale de CMake requise pour configurer le projet.
cmake_minimum_required(VERSION 3.16)

# Déclare le projet Stitching et indique que le langage C++ (CXX) est utilisé.
project(Stitching LANGUAGES CXX)

# Active la gestion des tests via CTest.
# Permet notamment d'utiliser BUILD_TESTING pour activer ou désactiver
# la compilation et l'exécution des tests unitaires.
include(CTest)

# Charge les dépendances externes nécessaires au projet.
# Ce fichier centralise notamment leur découverte et leur configuration.
include(cmake/dependencies_manager.cmake)

# Configure la bibliothèque backend.
# Le backend regroupe les traitements et fonctionnalités métier
# indépendants de l'interface graphique.
include(cmake/backend.cmake)

# Configure la bibliothèque frontend.
# Le frontend regroupe les composants liés à l'interface graphique.
include(cmake/frontend.cmake)

# Déclare l'exécutable principal de l'application.
# Le point d'entrée du programme est défini dans src/main.cpp.
add_executable(stitching src/main.cpp)

# Lie le backend à l'exécutable principal.
# PRIVATE signifie que cette dépendance est utilisée uniquement
# pour compiler et lier cet exécutable, sans être propagée aux cibles
# qui pourraient dépendre de stitching.
target_link_libraries(stitching PRIVATE kikirpa_backend)

# Configure les tests unitaires et leur intégration à CTest.
# Les tests peuvent être désactivés avec -DBUILD_TESTING=OFF.
include(cmake/tests_runner.cmake)

# Configure les règles d'installation du projet.
# Définit notamment les cibles et fichiers à installer.
include(cmake/init_setup.cmake)
