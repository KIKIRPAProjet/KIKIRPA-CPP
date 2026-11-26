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

# Certaines installations placent OpenCVConfig.cmake dans un chemin qui fait
# échouer le calcul de son préfixe et laissent OpenCV_INCLUDE_DIRS vide.
if(NOT OpenCV_INCLUDE_DIRS)
    find_path(KIKIRPA_OPENCV_INCLUDE_DIR
        NAMES opencv2/core/version.hpp
        HINTS "${OpenCV_INSTALL_PATH}/include"
        PATH_SUFFIXES opencv4
    )
    if(NOT KIKIRPA_OPENCV_INCLUDE_DIR)
        message(FATAL_ERROR "Could not locate OpenCV 4 headers")
    endif()
    set(OpenCV_INCLUDE_DIRS "${KIKIRPA_OPENCV_INCLUDE_DIR}")
endif()

# Recherche libtiff.
# Cette bibliothèque est notamment utilisée pour la lecture et l'écriture
# de fichiers TIFF tuilés et pyramidaux.
find_package(TIFF REQUIRED)
find_package(JPEG REQUIRED)

# Recherche Qt 6 et ses modules nécessaires à l'interface graphique.
#
# - Widgets : composants graphiques de l'application.
# - Concurrent : gestion des traitements concurrents et des tâches asynchrones.
# - Gui : lecture des images via QImageReader et les plugins Qt imageformats.
#
# REQUIRED indique que ces modules sont indispensables à la configuration
# du projet.
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Gui)

# Qt fournit QImageReader avec Qt6::Gui, mais le format TIFF est chargé par
# le plugin Qt Image Formats. Il ne s'agit pas d'une cible CMake Qt distincte :
# vérifier directement la présence du plugin rend la dépendance explicite.
set(KIKIRPA_QT_IMAGE_FORMATS_DIRS "")

foreach(KIKIRPA_QT_DIR
        "${Qt6Gui_DIR}"
        "${Qt6Core_DIR}"
        "${Qt6_DIR}")
    if(KIKIRPA_QT_DIR)
        get_filename_component(KIKIRPA_QT_PREFIX "${KIKIRPA_QT_DIR}/../../.." ABSOLUTE)
        list(APPEND KIKIRPA_QT_IMAGE_FORMATS_DIRS
            "${KIKIRPA_QT_PREFIX}/plugins/imageformats"
            "${KIKIRPA_QT_PREFIX}/share/qt/plugins/imageformats"
        )
    endif()
endforeach()

foreach(KIKIRPA_PREFIX IN LISTS CMAKE_PREFIX_PATH)
    list(APPEND KIKIRPA_QT_IMAGE_FORMATS_DIRS
        "${KIKIRPA_PREFIX}/plugins/imageformats"
        "${KIKIRPA_PREFIX}/share/qt/plugins/imageformats"
    )
endforeach()

if(APPLE)
    list(APPEND KIKIRPA_QT_IMAGE_FORMATS_DIRS
        "/opt/homebrew/share/qt/plugins/imageformats"
        "/opt/homebrew/opt/qtimageformats/share/qt/plugins/imageformats"
        "/opt/homebrew/opt/qt/share/qt/plugins/imageformats"
        "/usr/local/share/qt/plugins/imageformats"
        "/usr/local/opt/qtimageformats/share/qt/plugins/imageformats"
        "/usr/local/opt/qt/share/qt/plugins/imageformats"
    )
endif()

list(APPEND KIKIRPA_QT_IMAGE_FORMATS_DIRS
    "/usr/lib/qt6/plugins/imageformats"
    "/usr/lib/x86_64-linux-gnu/qt6/plugins/imageformats"
)
list(REMOVE_DUPLICATES KIKIRPA_QT_IMAGE_FORMATS_DIRS)

set(KIKIRPA_QT_IMAGE_FORMATS_PLUGIN "")
foreach(KIKIRPA_QT_IMAGE_FORMATS_DIR IN LISTS KIKIRPA_QT_IMAGE_FORMATS_DIRS)
    file(GLOB KIKIRPA_QT_IMAGE_FORMATS_PLUGIN_CANDIDATES
        "${KIKIRPA_QT_IMAGE_FORMATS_DIR}/qtiff*.dll"
        "${KIKIRPA_QT_IMAGE_FORMATS_DIR}/libqtiff.so*"
        "${KIKIRPA_QT_IMAGE_FORMATS_DIR}/libqtiff*.dylib"
    )
    if(KIKIRPA_QT_IMAGE_FORMATS_PLUGIN_CANDIDATES)
        list(GET KIKIRPA_QT_IMAGE_FORMATS_PLUGIN_CANDIDATES 0
            KIKIRPA_QT_IMAGE_FORMATS_PLUGIN)
        break()
    endif()
endforeach()

if(NOT KIKIRPA_QT_IMAGE_FORMATS_PLUGIN)
    message(FATAL_ERROR
        "Qt Image Formats plugin is missing; TIFF support via QImageReader is unavailable. "
        "Ubuntu: sudo apt-get install qt6-image-formats-plugins. "
        "macOS (Homebrew): brew install qtimageformats. "
        "Windows (aqt): python -m aqt install-qt windows desktop "
        "<version> win64_msvc2022_64 -O C:\\Qt -m qtimageformats --archives qtbase qtimageformats.")
endif()

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
# Par défaut, ONNX Runtime 1.20.1 est téléchargé pendant la configuration. Pour
# travailler hors ligne ou avec une autre version, indiquer son dossier :
# -DONNXRUNTIME_ROOT=<chemin> (archive extraite avec include/ et lib/, ou
# installation système, par exemple /usr sous Debian).
#
# Depuis le 17/11/2026, les releases GitHub d'ONNX Runtime sont en 404 :
# - Windows télécharge le paquet NuGet officiel de Microsoft (même version) ;
# - Linux et macOS doivent fournir ONNXRUNTIME_ROOT (voir docs/BUILD.md).
set(ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime directory (downloaded if empty)")
set(onnxruntime_version 1.20.1)

if(ONNXRUNTIME_ROOT)
    set(onnxruntime_include_dirs "${ONNXRUNTIME_ROOT}/include" "${ONNXRUNTIME_ROOT}/include/onnxruntime")
    set(onnxruntime_library_dirs "${ONNXRUNTIME_ROOT}/lib")
    if(CMAKE_LIBRARY_ARCHITECTURE)
        # Installation Debian / Ubuntu : /usr/lib/x86_64-linux-gnu
        list(APPEND onnxruntime_library_dirs "${ONNXRUNTIME_ROOT}/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
    endif()
else()
    include(FetchContent)
    if(WIN32)
        # Un .nupkg est un zip : DOWNLOAD_NAME lui donne une extension que CMake sait extraire.
        # Empreinte : SHA512 publiée par le catalogue nuget.org pour ce paquet.
        FetchContent_Declare(
            onnxruntime
            URL https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${onnxruntime_version}/microsoft.ml.onnxruntime.${onnxruntime_version}.nupkg
            URL_HASH SHA512=fc867bb4ee317aaa613460079fa754f80b5ee649ae826b102b0269976a89040ab8aed9768f68505a5527f8aa3161cc12e13a08c382b35d57731ee84c6b0d478d
            DOWNLOAD_NAME onnxruntime-${onnxruntime_version}.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(onnxruntime)
        set(onnxruntime_include_dirs "${onnxruntime_SOURCE_DIR}/build/native/include")
        set(onnxruntime_library_dirs "${onnxruntime_SOURCE_DIR}/runtimes/win-x64/native")
    else()
        # Archives GitHub, indisponibles depuis le 17/11/2026 : gardées pour le jour où elles reviennent.
        if(APPLE)
            set(onnxruntime_archive onnxruntime-osx-universal2-${onnxruntime_version}.tgz)
            set(onnxruntime_sha256 da4349e01a7e997f5034563183c7183d069caadc1d95f499b560961787813efd)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
            set(onnxruntime_archive onnxruntime-linux-x64-${onnxruntime_version}.tgz)
            set(onnxruntime_sha256 67db4dc1561f1e3fd42e619575c82c601ef89849afc7ea85a003abbac1a1a105)
        else()
            message(FATAL_ERROR "No ONNX Runtime archive for this system: set ONNXRUNTIME_ROOT")
        endif()
        FetchContent_Declare(
            onnxruntime
            URL https://github.com/microsoft/onnxruntime/releases/download/v${onnxruntime_version}/${onnxruntime_archive}
            URL_HASH SHA256=${onnxruntime_sha256}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(onnxruntime)
        set(onnxruntime_include_dirs "${onnxruntime_SOURCE_DIR}/include")
        set(onnxruntime_library_dirs "${onnxruntime_SOURCE_DIR}/lib")
    endif()
endif()

# Recherche refaite à chaque configuration : un build existant garderait sinon
# les chemins de l'ancienne archive, qui disparaissent quand FetchContent extrait
# la nouvelle au même endroit.
unset(ONNXRUNTIME_INCLUDE_DIR CACHE)
unset(ONNXRUNTIME_LIB CACHE)

find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    PATHS ${onnxruntime_include_dirs}
    NO_DEFAULT_PATH
    REQUIRED
)

find_library(ONNXRUNTIME_LIB
    NAMES onnxruntime
    PATHS ${onnxruntime_library_dirs}
    NO_DEFAULT_PATH
    REQUIRED
)
