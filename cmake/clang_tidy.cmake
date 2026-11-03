# Analyse statique avec clang-tidy

# Option désactivée par défaut : clang-tidy ne tourne que si on le demande,
# par exemple avec le preset linux-clang-tidy ou -DENABLE_CLANG_TIDY=ON.
option(ENABLE_CLANG_TIDY "Run clang-tidy during C++ builds" OFF)

if(ENABLE_CLANG_TIDY)

    # clang-tidy doit analyser le code avec les options d'un compilateur Clang.
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "ENABLE_CLANG_TIDY requires configuring with a Clang C++ compiler")
    endif()

    # Recherche l'exécutable clang-tidy dans le PATH.
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(NOT CLANG_TIDY_EXE)
        message(FATAL_ERROR "ENABLE_CLANG_TIDY is ON, but clang-tidy was not found")
    endif()

    # Toute cible C++ créée après cette ligne passe chacun de ses fichiers
    # dans clang-tidy à la compilation (règles dans .clang-tidy, à la racine).
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")

endif()
