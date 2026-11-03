# Dépendances externes

# Recherche OpenCV 4.
# REQUIRED indique que la configuration échoue si OpenCV n'est pas disponible.
# OpenCV est utilisé pour le traitement des images et les différents
# algorithmes du projet.
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs stitching OPTIONAL_COMPONENTS dnn)

# Recherche libtiff.
# Cette bibliothèque est notamment utilisée pour la lecture et l'écriture
# de fichiers TIFF tuilés et pyramidaux.
find_package(TIFF REQUIRED)

# Recherche Qt 6 et ses modules nécessaires à l'interface graphique.
#
# - Widgets : composants graphiques de l'application.
# - Concurrent : gestion des traitements concurrents et des tâches asynchrones.
#
# REQUIRED indique que ces modules sont indispensables à la configuration
# du projet.
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

# nlohmann_json

# Recherche une installation existante de nlohmann_json en version 3 minimum.
#
# QUIET évite l'affichage d'un avertissement si la bibliothèque n'est pas
# trouvée : une solution de remplacement est prévue avec FetchContent.
find_package(nlohmann_json 3 QUIET)

if(NOT nlohmann_json_FOUND)

    # La bibliothèque n'est pas disponible sur le système.
    # FetchContent permet alors de télécharger et d'intégrer automatiquement
    # ses sources pendant la configuration CMake.
    include(FetchContent)

    # Déclare la source de nlohmann_json à récupérer.
    # La version est explicitement fixée à 3.11.3 pour garantir
    # un comportement reproductible.
    FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )

    # Télécharge et configure la dépendance.
    # Le projet expose notamment la cible nlohmann_json::nlohmann_json,
    # utilisable directement avec target_link_libraries().
    FetchContent_MakeAvailable(json)

endif()
