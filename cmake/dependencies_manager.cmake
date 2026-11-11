# Dépendances externes

# Recherche OpenCV 4.
# REQUIRED indique que la configuration échoue si OpenCV n'est pas disponible.
# OpenCV est utilisé pour le traitement des images et les différents
# algorithmes du projet.
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs stitching dnn)

# Certaines configurations de paquetages acceptent une version plus récente
# que celle demandée ci-dessus. OpenCV 5 n'est pas encore pris en charge.
if(OpenCV_VERSION VERSION_GREATER_EQUAL "5.0")
    message(FATAL_ERROR
        "OpenCV ${OpenCV_VERSION} is not supported. This project requires OpenCV 4.x.")
endif()

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

# ONNX Runtime

# Moteur d'inférence du modèle LightGlue.
#
# Par défaut, l'archive officielle est téléchargée pendant la configuration :
# rien à installer, et la même version partout. Pour travailler hors ligne ou
# avec une autre version, indiquer son dossier : -DONNXRUNTIME_ROOT=<chemin>
set(ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime directory (downloaded if empty)")

if(ONNXRUNTIME_ROOT)
    set(onnxruntime_directory "${ONNXRUNTIME_ROOT}")
else()
    set(onnxruntime_version 1.20.1)

    if(WIN32)
        set(onnxruntime_archive onnxruntime-win-x64-${onnxruntime_version}.zip)
        set(onnxruntime_sha256 78d447051e48bd2e1e778bba378bec4ece11191c9e538cf7b2c4a4565e8f5581)
    elseif(APPLE)
        set(onnxruntime_archive onnxruntime-osx-universal2-${onnxruntime_version}.tgz)
        set(onnxruntime_sha256 da4349e01a7e997f5034563183c7183d069caadc1d95f499b560961787813efd)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        set(onnxruntime_archive onnxruntime-linux-x64-${onnxruntime_version}.tgz)
        set(onnxruntime_sha256 67db4dc1561f1e3fd42e619575c82c601ef89849afc7ea85a003abbac1a1a105)
    else()
        message(FATAL_ERROR "No ONNX Runtime archive for this system: set ONNXRUNTIME_ROOT")
    endif()

    include(FetchContent)
    FetchContent_Declare(
        onnxruntime
        URL https://github.com/microsoft/onnxruntime/releases/download/v${onnxruntime_version}/${onnxruntime_archive}
        URL_HASH SHA256=${onnxruntime_sha256}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(onnxruntime)
    set(onnxruntime_directory "${onnxruntime_SOURCE_DIR}")
endif()

find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    PATHS "${onnxruntime_directory}/include" "${onnxruntime_directory}/include/onnxruntime"
    NO_DEFAULT_PATH
    REQUIRED
)

find_library(ONNXRUNTIME_LIB
    NAMES onnxruntime
    PATHS "${onnxruntime_directory}/lib"
    NO_DEFAULT_PATH
    REQUIRED
)
