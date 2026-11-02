if(BUILD_TESTING)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        find_package(GTest REQUIRED)
    endif()

    # Ajouter un subdirectory lors de tests suplementaires
    add_subdirectory(tests/seamline)
endif()
