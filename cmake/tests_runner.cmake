# Configuration des tests unitaires

# Vérifie si la compilation des tests est activée.
# BUILD_TESTING est défini par CTest et vaut ON par défaut.
# Il peut être désactivé lors de la configuration avec :
# -DBUILD_TESTING=OFF
if(BUILD_TESTING)

    # Recherche GoogleTest via son package CMake.
    # CONFIG privilégie le fichier de configuration fourni par l'installation
    # de GoogleTest, notamment celle de vcpkg.
    # QUIET évite d'afficher un avertissement si le package n'est pas trouvé.
    find_package(GTest CONFIG QUIET)

    # Si le package de configuration n'a pas été trouvé,
    # tente une recherche classique de GoogleTest.
    # REQUIRED provoque une erreur si aucune installation n'est disponible.
    if(NOT GTest_FOUND)
        find_package(GTest REQUIRED)
    endif()

    # Diffère la découverte des tests jusqu'à leur exécution par CTest.
    #
    # PRE_TEST évite de lancer les exécutables de tests pendant la configuration
    # CMake. C'est notamment nécessaire sous Windows, où les DLL d'OpenCV
    # ne sont ajoutées au PATH qu'au moment de l'exécution via le preset :
    # ctest --preset windows-aqt
    set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

    # Ajoute les sous-répertoires contenant les différents ensembles de tests.
    # Chaque sous-répertoire possède son propre CMakeLists.txt et définit
    # les exécutables de tests associés.
    #
    # - brightness_equalization : tests de l'égalisation de luminosité.
    # - core                    : tests des fonctionnalités fondamentales du backend.
    # - phase_correlation       : tests de la corrélation de phase.
    # - seamline                : tests liés à la génération des seamlines.
    # - steps                   : tests des steps d'appariement.
    add_subdirectory(tests/brightness_equalization)
    add_subdirectory(tests/intensity_correction)
    add_subdirectory(tests/core)
    add_subdirectory(tests/phase_correlation)
    add_subdirectory(tests/seamline)
    add_subdirectory(tests/steps)
    add_subdirectory(tests/cli)

endif()
