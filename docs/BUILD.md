# Compiler KIKIRPA-CPP

## Dépendances

| Dépendance | Obligatoire | Rôle |
|---|---|---|
| CMake ≥ 3.21 (presets) et Ninja | oui | build |
| Compilateur C++17 | oui | MSVC 2022 sous Windows, GCC ou Clang sous Linux |
| OpenCV 4 | oui | images, algorithmes |
| libtiff | oui | TIFF tuilés pyramidaux (`core/PyramidalTiffWriter`) |
| nlohmann_json 3 | oui | téléchargé automatiquement (`FetchContent`) s'il n'est pas installé |
| Qt 6 (Widgets, Concurrent) | oui | GUI (`stitching_frontend`) |
| GoogleTest | oui, sauf avec `-DBUILD_TESTING=OFF` | tests unitaires (`ctest`) |
| ONNX Runtime + module `dnn` d'OpenCV | non | LightGlue seulement (`-DONNXRUNTIME_ROOT=<dossier>`) |

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
6. **Compiler et tester**, depuis un terminal « x64 Native Tools Command Prompt for VS 2022 », ou après avoir lancé `vcvars64.bat` :
   ```bat
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

## Linux (Ubuntu 24.04)

```bash
sudo apt-get install build-essential cmake ninja-build libopencv-dev libtiff-dev nlohmann-json3-dev \
    qt6-base-dev libgtest-dev
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
```

C'est la configuration de la CI (`.github/workflows/build-test.yml`).
