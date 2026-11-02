# KIKIRPA-CPP

Application C++ de traitement et d’assemblage d’images. Le projet est en cours de développement : l’interface et le point d’entrée `stitching` ne proposent pas encore de fonctions utilisateur.

## Prérequis

- CMake 3.16 ou plus récent et un compilateur C++17
- OpenCV, libTIFF et Qt 6 (modules Widgets et Concurrent)
- GoogleTest pour compiler les tests (activés par défaut)
- nlohmann-json 3 ou plus récent ; CMake le télécharge si aucun paquet compatible n’est installé

LightGlue est optionnel. Pour l’activer, installez ONNX Runtime ainsi que le module `dnn` d’OpenCV et indiquez son dossier d’installation avec `ONNXRUNTIME_ROOT`.

## Configurer et compiler

Depuis la racine du dépôt :

```sh
cmake -S . -B build
cmake --build build --parallel
```

Avec Visual Studio sous Windows :

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Si CMake ne trouve pas les dépendances, indiquez leurs préfixes d’installation avec `CMAKE_PREFIX_PATH`. Pour ONNX Runtime :

```sh
cmake -S . -B build -DONNXRUNTIME_ROOT=/chemin/vers/onnxruntime
```

## Tester

```sh
ctest --test-dir build --output-on-failure
```

Sous Windows avec une configuration multi-config :

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Les tests GoogleTest de seamline sont dans `tests/seamline`. Pour compiler sans les tests, configurez avec `-DBUILD_TESTING=OFF`.

## Installer

```sh
cmake --install build --prefix build/install
```

Sous Windows :

```powershell
cmake --install build --config Release --prefix build/install
```

L’installation place actuellement l’exécutable `stitching` dans `bin`. Les dépendances d’exécution, notamment les DLL sous Windows, ne sont pas incluses et doivent être installées séparément.
