# Compiler KIKIRPA-CPP

## Dépendances

| Dépendance | Obligatoire | Rôle |
|---|---|---|
| CMake ≥ 3.21 (presets) et Ninja | oui | build |
| Compilateur C++17 | oui | MSVC 2022 sous Windows, GCC ou Clang sous Linux |
| OpenCV 4 | oui | images, algorithmes |
| libtiff | oui | TIFF tuilés pyramidaux (`core/PyramidalTiffWriter`) |
| nlohmann_json 3 | oui | téléchargé automatiquement (`FetchContent`) s'il n'est pas installé |
| Qt 6 (Widgets, Concurrent) | non | GUI seulement ; sans Qt, `stitching_frontend` n'est pas construit |
| ONNX Runtime + module `dnn` d'OpenCV | non | LightGlue seulement (`-DONNXRUNTIME_ROOT=<dossier>`) |

## Windows (MSVC)

Les binaires officiels d'OpenCV pour Windows ne se lient **qu'avec MSVC**, pas avec MinGW.

1. **Build Tools Visual Studio 2022** (charge de travail « Développement Desktop en C++»).
2. **OpenCV** : binaires officiels extraits dans `C:\opencv`.
3. **libtiff via vcpkg**, une seule fois par poste :
   ```bat
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   C:\vcpkg\vcpkg install tiff:x64-windows
   ```
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

## Linux (Ubuntu 24.04)

```bash
sudo apt-get install build-essential cmake ninja-build libopencv-dev libtiff-dev nlohmann-json3-dev
# GUI, facultatif : sudo apt-get install qt6-base-dev
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
```

C'est la configuration de la CI (`.github/workflows/build-test.yml`).
