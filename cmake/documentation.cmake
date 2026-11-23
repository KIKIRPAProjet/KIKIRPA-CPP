# Configure la génération de la documentation API avec Doxygen.
find_package(Doxygen QUIET)

if(Doxygen_FOUND)
    set(KIKIRPA_DOXYFILE "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.kikirpa")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/doxygen.conf"
        "${KIKIRPA_DOXYFILE}"
        @ONLY
    )

    add_custom_target(stitching_docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${KIKIRPA_DOXYFILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Génération de la documentation Doxygen"
        VERBATIM
    )
else()
    message(STATUS "Doxygen introuvable : la cible stitching_docs est désactivée")
endif()
