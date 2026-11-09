# KIKIRPA-CPP

Logiciel d'assemblage (stitching) en C++ : il reconstruit une seule image d'une œuvre à partir de tuiles photographiées qui se recouvrent. Il est écrit en C++17, avec OpenCV, libtiff, ONNX Runtime et Qt 6.

Le travail est découpé en étapes (*steps*) enchaînées par un pipeline décrit en JSON :

```
import → appariement → positionnement → intensité → seamline → assemblage → export
```

**État (08/11/2026)** : les étapes jusqu'à la seamline existent. L'assemblage, l'export et le lien entre l'interface Qt et le calcul sont en cours.

Un pipeline JSON se lance avec `build/<preset>/stitching --pipeline <script.json>` (ajouter `.exe` sous Windows). Sans cette option, le point d'entrée réserve le lancement au frontend, encore en développement sur cette branche.

## Par où commencer

| Pour… | Lire |
|---|---|
| **comprendre le projet** : ce que fait chaque step, ses entrées et sorties, comment le lancer, ce qui reste à faire | [docs/COMPRENDRE_LE_PROJET.md](docs/COMPRENDRE_LE_PROJET.md) |
| compiler et lancer les tests | [docs/BUILD.md](docs/BUILD.md) |
| se repérer dans le code | [docs/CARTE_ARCHITECTURE.md](docs/CARTE_ARCHITECTURE.md) |
| écrire du code, ouvrir une pull request | [docs/CONVENTIONS.md](docs/CONVENTIONS.md) |
| le script JSON, la reprise et les checkpoints | [docs/core/Pipeline.md](docs/core/Pipeline.md) |
| un step ou un algorithme en détail : ses entrées, ses sorties, ses paramètres | [docs/steps/](docs/steps/), [docs/algorithms/](docs/algorithms/) |
| ajouter un step | [docs/steps/AJOUTER_UN_STEP.md](docs/steps/AJOUTER_UN_STEP.md) |

## Compiler en bref

```sh
cmake --preset <windows-aqt | linux | macos>
cmake --build --preset <même preset>
ctest --preset <même preset>
```

Avant cela, il faut installer les dépendances : voir [docs/BUILD.md](docs/BUILD.md).

## Organisation du dépôt

```
include/backend, src/backend   moteur (core/), steps (steps/), algorithmes (algorithms/)
include/frontend, src/frontend interface Qt
tests/                         tests unitaires GoogleTest
docs/                          documentation
cmake/                         modules CMake (dépendances, cibles, tests, installation)
```
