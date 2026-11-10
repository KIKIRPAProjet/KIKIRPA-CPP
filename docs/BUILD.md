# Compiler KIKIRPA-CPP

Première installation sur un poste, pas à pas, avec les problèmes courants : [DEMARRER.md](DEMARRER.md). Cette page est la référence : dépendances, presets et options du build.

## Dépendances

| Dépendance | Obligatoire | Rôle |
|---|---|---|
| CMake ≥ 3.21 (presets) et Ninja | oui | build |
| Compilateur C++17 | oui | MSVC 2022 sous Windows, GCC ou Clang sous Linux |
| OpenCV 4, avec le module `dnn` | oui | images, algorithmes |
| libtiff | oui | TIFF tuilés pyramidaux (`core/io/pyramidal_tiff_writer`) |
| nlohmann_json 3 | oui | téléchargé automatiquement (`FetchContent`) s'il n'est pas installé |
| ONNX Runtime 1.20.1 | oui | LightGlue ; téléchargé automatiquement à la configuration |
| Qt 6 (Widgets, Concurrent) | oui | GUI (`kikirpa_frontend`) |
| GoogleTest | oui, sauf avec `-DBUILD_TESTING=OFF` | tests unitaires (`ctest`) |
| Clang et clang-tidy | non | vérifications statiques des sources C++ (`ENABLE_CLANG_TIDY=ON`) |

## Windows (MSVC)

Les binaires officiels d'OpenCV pour Windows ne se lient **qu'avec MSVC**, pas avec MinGW.

1. **Visual Studio 2022** (Community ou Build Tools), avec la charge de travail « Développement Desktop en C++ ».
2. **OpenCV 4** : télécharger les binaires officiels pour Windows depuis [opencv.org](https://opencv.org/releases/) et extraire l'archive dans `C:\`. Le dossier de configuration doit être `C:\opencv\build`.
3. **libtiff et GoogleTest via vcpkg** (pas Qt) :
   ```bat
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   C:\vcpkg\vcpkg install tiff gtest --triplet x64-windows
   ```
   Si vcpkg est déjà installé, ne relancez pas `git clone` ni le bootstrap ; exécutez seulement la dernière commande.
4. **Qt 6 binaire avec aqt**. Installer Python si nécessaire, puis exécuter :
   ```bat
   python -m pip install --user aqtinstall
   python -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -O C:\Qt --archives qtbase
   ```
   Cette commande installe Qt Widgets et Concurrent sans les compiler depuis les sources.
5. **Variables d'environnement** utilisateur, une seule fois, puis rouvrir le terminal ou l'IDE :
   ```bat
   setx KIKIRPA_VCPKG_ROOT C:\vcpkg
   setx OpenCV_DIR C:\opencv\build
   setx QT_ROOT C:\Qt\6.10.3\msvc2022_64
   ```
   `KIKIRPA_VCPKG_ROOT` évite le conflit avec la variable `VCPKG_ROOT` que Visual Studio peut définir pour son propre vcpkg.
6. **Compiler et tester** avec `python scripts/demarrer.py --tests`, depuis n'importe quel terminal ([DEMARRER.md](DEMARRER.md) § 4). À la main : depuis un terminal « x64 Native Tools Command Prompt for VS 2022 », ou après avoir lancé `vcvars64.bat`, avec la console en UTF-8 :
   ```bat
   chcp 65001
   cmake --preset windows-aqt
   cmake --build --preset windows-aqt
   ctest --preset windows-aqt
   ```
   Lancez ces commandes depuis la racine du dépôt (le dossier qui contient `CMakePresets.json`). Le build est dans `build/windows-aqt/`. Les DLL de libtiff et GoogleTest sont copiées à côté des exécutables par vcpkg. Celle d'OpenCV doit être dans le `PATH` (`%OpenCV_DIR%\x64\vc16\bin`) ; `ctest --preset` l'ajoute tout seul.

**CLion** : *Settings → Build, Execution, Deployment → Toolchains*, choisir **Visual Studio** (pas MinGW). Les presets apparaissent ensuite dans *CMake → Profiles*.

## macOS (Homebrew)

Installer les dépendances une seule fois :

```bash
brew install cmake opencv libtiff qtbase googletest
```

Dans VS Code, choisir le preset **macos** lorsqu'il est demandé, ou utiliser :

```bash
cmake --preset macos
cmake --build --preset macos
ctest --preset macos
```

Le preset sélectionne Apple Clang et les chemins Homebrew Qt sur Apple Silicon
(`/opt/homebrew`) et Intel (`/usr/local`). Le build est généré dans `build/macos/`.

Pour l'exécution locale de l'application ou en cas de problème lié aux plugins Qt sous macOS, voir [RunOnMacos.md](building/RunOnMacos.md).

## Linux (Ubuntu 24.04)

```bash
sudo apt-get install build-essential cmake ninja-build libopencv-dev libtiff-dev nlohmann-json3-dev \
    qt6-base-dev libgtest-dev
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
```

C'est la configuration de la CI (`.github/workflows/build-test.yml`).

### Vérifications clang-tidy

Installer Clang et `clang-tidy`, puis configurer et compiler avec le preset
dédié (qui sélectionne `clang++`) :

```bash
sudo apt-get install clang-tidy
cmake --preset linux-clang-tidy
cmake --build --preset linux-clang-tidy
```

Le fichier `.clang-tidy` active les contrôles `bugprone`, `clang-analyzer` et
`performance`, ainsi que les vérifications de conventions de nommage, de
longueur et d'imbrication des fonctions, de nombres magiques, de casts C et
d'includes inutilisés. Il vérifie aussi quelques pratiques C++ (`std::make_unique`,
`override`, `nullptr`, retours anticipés et absence de `catch` vide). Les seuils
de longueur, d'imbrication et de paramètres reprennent les repères de
[`CONVENTIONS.md`](CONVENTIONS.md); les règles qui ne peuvent pas être contrôlées
automatiquement restent à vérifier en revue de code. Les parties du code qui ne
sont pas encore migrées peuvent aussi produire des diagnostics. Ceux-ci sont
affichés pendant la compilation et ne font pas échouer le build par défaut.
Pour activer cette vérification dans un autre build CMake Clang, ajouter
`-DENABLE_CLANG_TIDY=ON` à la configuration. La GitHub Action CI exécute aussi
clang-tidy uniquement sur les fichiers C++ qu'une branche ou une pull request
modifie par rapport à `main`, et fait échouer le job si un diagnostic est émis.

## ONNX Runtime et modèle LightGlue

Rien à installer : à la première configuration, CMake télécharge l'archive officielle d'ONNX Runtime 1.20.1 dans `build/.../_deps`, en vérifiant son empreinte SHA256. Si les tests sont activés, il télécharge aussi le modèle de test (environ 63 Mo) dans `build/.../models`. Une connexion internet est donc nécessaire la première fois.

Pour travailler hors ligne, indiquer des fichiers déjà présents :

```bash
cmake --preset <preset> -DONNXRUNTIME_ROOT=<dossier> -DLIGHTGLUE_TEST_MODEL=<fichier.onnx>
```

Sous Windows, `System32` contient souvent un `onnxruntime.dll` plus ancien, chargé avant le `PATH` : CMake copie donc les DLL à côté des exécutables. Le modèle et sa provenance sont indiqués dans [LightGlueStep.md](steps/LightGlueStep.md).

## Désactiver les tests unitaires

GoogleTest est obligatoire, sauf si les tests sont désactivés :

```bash
cmake --preset <preset> -DBUILD_TESTING=OFF
```

Remplacer `<preset>` par celui de son système : `windows-aqt`, `linux` ou `macos`.

## Organisation du build

| Fichier | Rôle |
|---|---|
| `CMakeLists.txt` | Point d'entrée : inclut les fichiers de `cmake/` dans l'ordre et déclare l'exécutable `stitching` (nom provisoire). |
| `cmake/clang_tidy.cmake` | Option `ENABLE_CLANG_TIDY`. |
| `cmake/dependencies_manager.cmake` | Recherche d'OpenCV, libtiff, Qt 6 ; téléchargement de nlohmann_json s'il manque. |
| `cmake/backend.cmake` | Bibliothèque `kikirpa_backend` (tout `src/backend/`), LightGlue compris. |
| `cmake/frontend.cmake` | Bibliothèque `kikirpa_frontend` (`src/frontend/`, Qt). |
| `cmake/tests_runner.cmake` | GoogleTest et un `add_subdirectory` par dossier de `tests/`. |
| `cmake/install.cmake` | Installation de `stitching` dans `bin/`. |

Un exécutable de test se lie à `kikirpa_backend` (voir `tests/brightness_equalization/CMakeLists.txt`).
