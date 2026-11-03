# Compiler KIKIRPA-CPP

## Dépendances

| Dépendance | Obligatoire | Rôle |
|---|---|---|
| CMake ≥ 3.21 (presets) et Ninja | oui | build |
| Compilateur C++17 | oui | MSVC 2022 sous Windows, GCC ou Clang sous Linux/macOS |
| OpenCV 4 | oui | images, algorithmes |
| libtiff | oui | TIFF tuilés pyramidaux (`core/PyramidalTiffWriter`) |
| nlohmann_json 3 | oui | téléchargé automatiquement (`FetchContent`) s'il n'est pas installé |
| Qt 6 (Widgets, Concurrent) | oui | GUI (`stitching_frontend`) |
| GoogleTest | oui, sauf avec `-DBUILD_TESTING=OFF` | tests unitaires (`ctest`) |
| ONNX Runtime + module `dnn` d'OpenCV | non | LightGlue seulement (`-DONNXRUNTIME_ROOT=<dossier>`) |

## Windows (MSVC)

Les binaires officiels d'OpenCV pour Windows se lient **uniquement avec MSVC**, pas avec MinGW.

### 1. Installer les outils de compilation

Installer **Build Tools Visual Studio 2022**, avec la charge de travail :

- Développement Desktop en C++.

Installer également :
- CMake ≥ 3.21
- Ninja
- Git
- Python 3

### 2. Installer OpenCV

Télécharger les binaires officiels d'OpenCV pour Windows et les extraire dans :

`C:\opencv`

### 3. Installer libtiff et GoogleTest via vcpkg

vcpkg est utilisé uniquement pour ces deux dépendances. Il n'est pas nécessaire de compiler Qt depuis les sources.

```bat
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
C:\vcpkg\vcpkg install tiff gtest --triplet x64-windows
```

Si vcpkg est déjà installé, seule la dernière commande est nécessaire.

### 4. Installer Qt 6 via aqtinstall

Qt est installé séparément via `aqtinstall`, qui télécharge directement les binaires précompilés pour MSVC.

Installer aqtinstall :

```bat
py -m pip install --user aqtinstall
```

Télécharger Qt 6 :

```bat
py -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
```

Qt sera installé dans :

`C:\Qt\6.8.3\msvc2022_64`

Cette distribution contient les modules Widgets et Concurrent nécessaires au projet.

La version Qt peut être adaptée, à condition de rester compatible avec le projet.

### 5. Configurer les variables d'environnement

Définir les variables d'environnement utilisateur une seule fois :

```bat
setx VCPKG_ROOT C:\vcpkg
setx OpenCV_DIR C:\opencv\build
setx Qt6_DIR C:\Qt\6.8.3\msvc2022_64\lib\cmake\Qt6
```

Rouvrir le terminal ou l'IDE après cette opération.

### 6. Compiler et tester

Ouvrir un terminal **x64 Native Tools Command Prompt for VS 2022**, ou lancer `vcvars64.bat`.

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

Le build est généré dans `build/windows-msvc/`.

Les DLL de libtiff sont copiées à côté des exécutables par vcpkg.

Les DLL d'OpenCV et de Qt doivent être accessibles dans le `PATH` pour exécuter les programmes :

```bat
C:\opencv\build\x64\vc16\bin
C:\Qt\6.8.3\msvc2022_64\bin
```

`ctest --preset` doit également disposer de ces chemins.

**CLion** : *Settings → Build, Execution, Deployment → Toolchains*, choisir **Visual Studio** (pas MinGW). Les presets apparaissent ensuite dans *CMake → Profiles*.

---

## Linux (Ubuntu 24.04)

Les dépendances peuvent être installées directement depuis les dépôts Ubuntu :

```bash
sudo apt-get update

sudo apt-get install \
    build-essential \
    cmake \
    ninja-build \
    libopencv-dev \
    libtiff-dev \
    nlohmann-json3-dev \
    qt6-base-dev \
    libgtest-dev
```

Qt est fourni par les paquets système. Il n'est pas nécessaire d'installer aqtinstall.

### Compiler et tester

Depuis la racine du projet :

```bash
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
```

C'est la configuration utilisée par la CI (`.github/workflows/build-test.yml`).

---

## macOS

### 1. Installer les outils nécessaires

Installer Xcode Command Line Tools :

```bash
xcode-select --install
```

Installer Homebrew si nécessaire : https://brew.sh

Installer les dépendances :

```bash
brew install \
    cmake \
    ninja \
    opencv \
    libtiff \
    nlohmann-json \
    qt
```

Pour GoogleTest :

```bash
brew install googletest
```

Qt est fourni par Homebrew. Il n'est pas nécessaire d'installer aqtinstall.

### 2. Configurer Qt

Homebrew installe généralement Qt dans un répertoire dépendant de l'architecture et de la configuration de Homebrew.

Récupérer son chemin :

```bash
brew --prefix qt
```

Si CMake ne trouve pas Qt automatiquement, définir `Qt6_DIR` :

```bash
export Qt6_DIR="$(brew --prefix qt)/lib/cmake/Qt6"
```

Pour conserver cette variable entre les sessions, ajouter la commande à `~/.zshrc`.

### 3. Compiler et tester

Depuis la racine du projet :

```bash
cmake --preset macos
cmake --build --preset macos
ctest --preset macos
```

Le build est généré dans `build/macos/`.

---

## Configuration optionnelle : LightGlue

ONNX Runtime est nécessaire uniquement pour utiliser LightGlue.

Il faut également disposer du module `dnn` d'OpenCV.

Pour activer cette fonctionnalité, installer ONNX Runtime pour son système d'exploitation et préciser son répertoire d'installation lors de la configuration CMake :

```bash
cmake --preset <preset> -DONNXRUNTIME_ROOT=<dossier>
```

Sans ONNX Runtime, le reste du projet peut être compilé normalement.

## Désactiver les tests unitaires

GoogleTest est requis sauf si les tests sont désactivés :

```bash
cmake --preset <preset> -DBUILD_TESTING=OFF
```

Remplacer `<preset>` par le preset correspondant à son système d'exploitation.