if(BUILD_TESTING)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        find_package(GTest REQUIRED)
    endif()

    # Liste des tests lue au lancement de ctest, pas après la compilation : sous Windows, les DLL d'OpenCV
    # ne sont dans le PATH qu'à ce moment-là (ctest --preset windows-aqt)
    set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

    # Ajouter un subdirectory lors de tests suplementaires
    add_subdirectory(tests/core)
    add_subdirectory(tests/correlation_phase)
    add_subdirectory(tests/egalisation)
    add_subdirectory(tests/seamline)
endif()
