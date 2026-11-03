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
| Clang et clang-tidy | non | vérifications statiques des sources C++ (`ENABLE_CLANG_TIDY=ON`) |
| ONNX Runtime + module `dnn` d'OpenCV | non | LightGlue seulement (`-DONNXRUNTIME_ROOT=<dossier>`) |

## Windows (MSVC)

Les binaires officiels d'OpenCV pour Windows ne se lient **qu'avec MSVC**, pas avec MinGW.

1. **Build Tools Visual Studio 2022** (charge de travail « Développement Desktop en C++»).
2. **OpenCV** : binaires officiels extraits dans `C:\opencv`.
3. **libtiff, GoogleTest et Qt 6 via vcpkg**, une seule fois par poste :
   ```bat
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   C:\vcpkg\vcpkg install tiff gtest qtbase --triplet x64-windows
   ```
   `qtbase` contient Widgets et Concurrent. Il est compilé depuis les sources : la première installation prend une à plusieurs heures, ensuite vcpkg le garde en cache. Si vcpkg était déjà installé pour libtiff, il suffit de relancer la dernière ligne.
4. **Variables d'environnement** utilisateur, une seule fois, puis rouvrir le terminal ou l'IDE :
   ```bat
   setx VCPKG_ROOT C:\vcpkg
   setx OpenCV_DIR C:\opencv\build
   ```
5. **Compiler et tester**, depuis un terminal « x64 Native Tools Command Prompt for VS 2022 », ou après avoir lancé `vcvars64.bat` :
   ```bat
   cmake --preset windows-msvc
   cmake --build --preset windows-msvc
   ctest --preset windows-msvc
   ```
   Le build est dans `build/windows-msvc/`. Les DLL de libtiff sont copiées à côté des exécutables par vcpkg. Celle d'OpenCV doit être dans le `PATH` (`%OpenCV_DIR%\x64\vc16\bin`) ; `ctest --preset` l'ajoute tout seul.

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
clang-tidy uniquement sur les fichiers C++ modifiés dans une pull request et
fait échouer le job si un diagnostic est émis.
