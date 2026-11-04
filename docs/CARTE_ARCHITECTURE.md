# Carte de l'architecture : KIKIRPA-CPP

Guide pour se repérer dans le code de **`main`**, à jour du 03/11/2026 après la réorganisation de `core/` et `steps/` (PR #50) et la migration des algorithmes (branche `refactor/algorithms-conventions`). Les éléments marqués **🆕** ont changé avec l'une ou l'autre : dossiers en minuscules, fichiers en `snake_case`, noms anglais, un namespace par dossier, nouveau format JSON. Correspondance complète entre anciens et nouveaux noms : [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md) (§12 pour les algorithmes). Règles de nommage : [CONVENTIONS.md](CONVENTIONS.md).

> **Cette carte doit rester à jour.** Elle est mise à jour, avec [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md), **après le merge** de chaque PR qui ajoute, déplace, renomme ou supprime un fichier, un step, un type partagé ou un test, dans une PR séparée. L'auteur d'une PR n'a pas à modifier ces deux docs.

Les liens ouvrent les fichiers.

**Sommaire**

- [Carte de l'architecture : KIKIRPA-CPP](#carte-de-larchitecture--kikirpa-cpp)
  - [1. Vue d'ensemble](#1-vue-densemble)
  - [2. Où aller pour…](#2-où-aller-pour)
  - [3. Comment le code est rangé](#3-comment-le-code-est-rangé)
  - [4. Arborescence annotée, fichier par fichier](#4-arborescence-annotée-fichier-par-fichier)
    - [Racine et build](#racine-et-build)
    - [`core/` : le moteur](#core--le-moteur)
    - [`steps/` : les étapes du pipeline](#steps--les-étapes-du-pipeline)
    - [`algorithms/stitching/` : appariement et positionnement 🆕](#algorithmsstitching--appariement-et-positionnement-)
    - [`algorithms/rejection/` : rejet des outliers 🆕](#algorithmsrejection--rejet-des-outliers-)
    - [`algorithms/brightness_equalization/` : égalisation de luminosité 🆕](#algorithmsbrightness_equalization--égalisation-de-luminosité-)
    - [`algorithms/seamline/` : les coutures 🆕](#algorithmsseamline--les-coutures-)
    - [`frontend/` : interface graphique](#frontend--interface-graphique)
    - [`tests/`](#tests)
    - [`docs/`](#docs)
  - [5. Les données qui circulent](#5-les-données-qui-circulent)
  - [6. Lire un algorithme : l'exemple de la corrélation de phase](#6-lire-un-algorithme--lexemple-de-la-corrélation-de-phase)
  - [7. Pièges de lecture](#7-pièges-de-lecture)
  - [8. Ce qui n'existe qu'en branche](#8-ce-qui-nexiste-quen-branche)

---

## 1. Vue d'ensemble

Le projet a trois couches. Les **algorithmes** sont les plus fournis. L'**orchestration** est en place : un script JSON se construit, se valide, s'exécute et se reprend. Il manque surtout des steps, et un point d'entrée pour l'utilisateur.

```txt
┌────────────────────────────────────────────────────────────────────────────┐
│ ENTRÉES              src/main.cpp (vide)  ·  src/frontend/ (GUI Qt)        │
│                      pas de ligne de commande : le pipeline se lance       │
│                      depuis le code (GUI à venir, tests)                   │
├────────────────────────────────────────────────────────────────────────────┤
│ ORCHESTRATION        core/  : script JSON → buildPipeline → PipelineClass  │
│                               → StepClass → DataStoreClass ; reprise :     │
│                               RunState, CheckpointClass                    │
│                      steps/ : une classe par étape du pipeline (StepsNs)   │
├────────────────────────────────────────────────────────────────────────────┤
│ ALGORITHMES          algorithms/ : un dossier par méthode, rangé par étape │
│                      (appelables seuls, sans pipeline)                     │
└────────────────────────────────────────────────────────────────────────────┘
```

**Le pipeline prévu**, avec ce qui existe sur `main` (✅ fait · ⚠️ partiel · ❌ absent) :

```mermaid
flowchart LR
    I[Import<br/>✅ import_step] --> R[Recouvrement<br/>algo ✅ · step ❌]
    R --> M[Appariement<br/>algos ✅ · step ⚠️ phase seule]
    M --> X[RANSAC<br/>algo ✅ · step ❌]
    X --> L[Moindres carrés<br/>algo ⚠️ · step ❌]
    L --> E[Intensité<br/>algo ✅ · step ❌]
    E --> S[Seamline<br/>algos ✅ · step ⚠️ GraphCut]
    S --> A[Assemblage<br/>❌]
    A --> O[Export<br/>❌]
```

Une **étape** (step) lit ses entrées dans le `DataStoreClass` et y écrit ses sorties. Pour faire le calcul, elle appelle un **algorithme**. L'algorithme, lui, ne connaît ni le pipeline ni le JSON : il prend des `cv::Mat` et des structures C++.

**Le chemin d'un run** :

```txt
script.json ──buildPipelineFromFile──► registre (nom → step) ──importConfig──► paramètres validés
            ──contrôle de l'enchaînement (chaque entrée produite plus tôt, même type)──► PipelineClass
PipelineClass::run ──► saute les steps déjà faits (checkpoint valide) et restaure leurs sorties
                   ──► pour chaque step : execute() → DataStoreClass → checkpoint → run.json réécrit
```

---

## 2. Où aller pour…

| Je veux…                                                                    | Fichier(s)                                                                                                                                                                                                                                |
| --------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Compiler le projet** (Windows, Linux ou macOS)                            | [BUILD.md](BUILD.md) (Qt 6 et GoogleTest obligatoires ; sous Windows, Qt binaire via aqt, libtiff et GoogleTest via vcpkg) + [CMakePresets.json](../CMakePresets.json) (`cmake --preset windows-aqt`)                                     |
| **Lancer les tests**                                                        | `ctest --preset windows-aqt` (ou `linux`, `macos`) : 87 tests GoogleTest, tous `unit`                                                                                                                                                     |
| **Lancer un pipeline**                                                      | depuis le code : `buildPipelineFromFile()` puis `run()`, voir [Pipeline.md](core/Pipeline.md#lancer-un-pipeline). Exemple qui tourne : `testerPipelineReel()` dans [test_pipeline.cpp](../tests/core/test_pipeline.cpp)                   |
| **Comprendre le pipeline en une page**                                      | [Pipeline.md](core/Pipeline.md) : format du script, contrôles, reprise, steps enregistrés, ajout d'un step                                                                                                                                |
| Voir comment un script JSON devient un pipeline                             | [pipeline_builder.cpp](../src/backend/core/pipeline/pipeline_builder.cpp) : `stepRegistry()`, `buildPipeline()`                                                                                                                           |
| **Ajouter un step au pipeline**                                             | une ligne dans `stepRegistry()` ([pipeline_builder.cpp](../src/backend/core/pipeline/pipeline_builder.cpp)) ; marche à suivre dans [Pipeline.md](core/Pipeline.md#ajouter-un-step)                                                        |
| **Comprendre comment le pipeline exécute les étapes**                       | [pipeline.h](../include/backend/core/pipeline/pipeline.h) → [pipeline.cpp](../src/backend/core/pipeline/pipeline.cpp) (`run()`, avec la reprise)                                                                                          |
| Voir comment un paramètre du JSON est validé et appliqué                    | [step.cpp](../src/backend/core/step/step.cpp) : `importConfig()`                                                                                                                                                                          |
| Reprendre un pipeline après un échec ou une pause                           | [run_state.h](../include/backend/core/pipeline/run_state.h) (champ `"run"`), [pipeline.cpp](../src/backend/core/pipeline/pipeline.cpp) (`resolveStart`, `run`)                                                                            |
| Voir ce qui est sauvegardé dans un checkpoint                               | [checkpoint.cpp](../src/backend/core/checkpoint/checkpoint.cpp) (manifest, validité) + [checkpoint_types.cpp](../src/backend/core/checkpoint/checkpoint_types.cpp) (un sérialiseur par type)                                              |
| Voir ce qu'une étape doit implémenter                                       | [step.h](../include/backend/core/step/step.h) (`execute()`, `getParameter()`, `result`, `externalInputsFingerprint()`) 🆕                                                                                                                 |
| Voir comment les données passent d'une étape à l'autre                      | [data_store.h](../include/backend/core/step/data_store.h) (`add` / `replace` / `get<T>("clé")`) 🆕                                                                                                                                        |
| Voir le format du script JSON du pipeline                                   | [pipeline_config.h](../include/backend/core/config/pipeline_config.h)                                                                                                                                                                     |
| **Voir tous les types partagés** (paires, matches, positions, territoires…) | [algorithm_types.h](../include/backend/core/data/algorithm_types.h) ← le fichier le plus important à connaître                                                                                                                            |
| Voir comment une tuile est représentée                                      | [tile.h](../include/backend/core/data/tile.h) (fiche sans pixels, pyramide, voisins)                                                                                                                                                      |
| Voir comment les images sont chargées                                       | [import_step.cpp](../src/backend/steps/import_step.cpp)                                                                                                                                                                                   |
| Voir le cache TIFF pyramidal créé à l'import                                | [PyramidalTiff.md](core/PyramidalTiff.md), [pyramidal_tiff_writer.cpp](../src/backend/core/io/pyramidal_tiff_writer.cpp) et [pyramidal_tiff_reader.cpp](../src/backend/core/io/pyramidal_tiff_reader.cpp)                                 |
| **Écrire un nouveau step** : modèle à copier                                | [phase_correlation_step.h](../include/backend/steps/phase_correlation_step.h) / [.cpp](../src/backend/steps/phase_correlation_step.cpp) et sa doc [PhaseCorrelationStep.md](steps/PhaseCorrelationStep.md) (contrat, conventions, échecs) |
| Mesurer le décalage entre deux tuiles (corrélation de phase)                | [phase_correlation_matcher.h](../include/backend/algorithms/stitching/phase_correlation/phase_correlation_matcher.h) + sa [doc](algorithms/stitching/phase_correlation/PhaseCorrelation.md) 🆕                                            |
| Estimer le recouvrement réel d'une paire (1re passe)                        | [overlap_estimator.h](../include/backend/algorithms/stitching/overlap_estimation/overlap_estimator.h) + sa [doc](algorithms/stitching/overlap_estimation/OverlapEstimation.md) 🆕                                                         |
| Voir l'appariement SIFT                                                     | [sift_matcher_algorithm.cpp](../src/backend/algorithms/stitching/sift/sift_matcher_algorithm.cpp) (le calcul) ; [sift_matcher.cpp](../src/backend/algorithms/stitching/sift/sift_matcher.cpp) (l'emballage) 🆕                            |
| Voir l'appariement LightGlue (ONNX)                                         | [lightglue_matcher.cpp](../src/backend/algorithms/stitching/lightglue/lightglue_matcher.cpp) 🆕                                                                                                                                           |
| Voir le rejet des mauvais points (vote de Hough 2D)                         | [vote_rejector.cpp](../src/backend/algorithms/rejection/vote_2d/vote_rejector.cpp) 🆕                                                                                                                                                     |
| **Voir l'alignement global** (moindres carrés)                              | [tile_positioner.cpp](../src/backend/algorithms/stitching/positioning/tile_positioner.cpp) → `solvePositions()` (namespace anonyme) 🆕                                                                                                    |
| **Égaliser la luminosité entre tuiles**                                     | [brightness_equalizer.h](../include/backend/algorithms/brightness_equalization/brightness_equalizer.h) + sa [doc](algorithms/brightness_equalization/BrightnessEqualization.md) 🆕                                                        |
| Voir le calcul des coutures, méthode DP/Dijkstra                            | [seam_finder.h](../include/backend/algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h) (point d'entrée) puis [solver.h](../include/backend/algorithms/seamline/dynamic_programming_dijkstra/solver.h) 🆕                      |
| Voir le calcul des coutures, méthode GraphCut                               | [graph_cut_seam_finder.cpp](../src/backend/algorithms/seamline/graph_cut/graph_cut_seam_finder.cpp) + [graph_cut_step.cpp](../src/backend/steps/graph_cut_step.cpp) 🆕                                                                    |
| Voir un vrai pipeline tourner depuis un script JSON                         | [test_pipeline.cpp](../tests/core/test_pipeline.cpp) : `testerPipelineReel()` (import → phase-correlation) et `testerRepriseReelle()`                                                                                                     |
| Générer le JSON du pipeline depuis la GUI                                   | [PipelineJson.h](../include/frontend/PipelineJson.h)                                                                                                                                                                                      |
| Voir comment le projet est compilé                                          | [CMakeLists.txt](../CMakeLists.txt) + [cmake/](../cmake/) (`clang_tidy`, `dependencies_manager`, `backend`, `frontend`, `tests_runner`, `install`)                                                                                        |
| **Nommer un fichier, une classe, une variable** 🆕                          | [CONVENTIONS.md](CONVENTIONS.md)                                                                                                                                                                                                          |
| Adapter une branche écrite avant la réorganisation 🆕                       | [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md) (§10 ; §12 pour les algorithmes)                                                                                                                                               |
| Voir ce que fait la CI                                                      | [build-test.yml](../.github/workflows/build-test.yml) (appelé par `ci.yml` et `release.yml`)                                                                                                                                              |

---

## 3. Comment le code est rangé

**`include/` et `src/` ont la même arborescence.** Pour un fichier `include/backend/X/y.h`, le `.cpp` est dans `src/backend/X/y.cpp`.

```txt
include/backend/  ⟷  src/backend/   🆕 en minuscules
    core/           le moteur, un sous-dossier par rôle 🆕 :
        data/           types partagés (algorithm_types.h), fiche d'une tuile (tile.h)
        config/         format du script JSON (pipeline_config.h)
        step/           contrat d'un step (step.h) et données entre steps (data_store.h)
        pipeline/       exécution, registre, état de reprise
        checkpoint/     sauvegarde des sorties de chaque step
        io/             écriture du TIFF pyramidal
    steps/          les étapes concrètes, une classe par étape (<nom>_step.h) 🆕
    algorithms/     les méthodes de calcul, un dossier et un namespace par méthode 🆕
        stitching/      appariement + recouvrement + positionnement global
        rejection/      rejet des outliers
        brightness_equalization/   égalisation de luminosité 🆕
        seamline/       calcul des coutures
include/frontend/ ⟷  src/frontend/   GUI Qt (quasi vide sur main)
tests/               un dossier par module, en GoogleTest : brightness_equalization/, core/, phase_correlation/, seamline/ 🆕
docs/                algorithms/ (une page par algorithme), core/ (le pipeline), steps/ (contrat de chaque step),
                     BUILD.md, CONVENTIONS.md 🆕, MIGRATION_REORGANISATION.md 🆕, cette carte
cmake/               un fichier par partie du build
```

**Les includes partent de `include/backend/`** 🆕, jamais de `"../"`. On écrit `#include "core/data/algorithm_types.h"` ou `#include "algorithms/stitching/sift/sift_matcher.h"`.

**Les namespaces** 🆕 : un par dossier, partout. Les algorithmes qualifient eux-mêmes les types partagés (`DataNs::Matches`) ; `algorithms/legacy_type_aliases.h` a été supprimé.

| Dossier                                                  | Namespace C++                                        |
| -------------------------------------------------------- | ---------------------------------------------------- |
| `core/data/`                                             | `DataNs` 🆕                                          |
| `core/config/`                                           | `ConfigNs` 🆕                                        |
| `core/step/`                                             | `StepNs` 🆕                                          |
| `core/pipeline/`                                         | `PipelineNs` 🆕                                      |
| `core/checkpoint/`                                       | `CheckpointNs` 🆕                                    |
| `core/io/`                                               | `IONs` 🆕                                            |
| `steps/`                                                 | `StepsNs` 🆕                                         |
| `frontend/`                                              | `FrontendNs` 🆕                                      |
| `algorithms/stitching/` (interface `feature_matcher.h`)  | `StitchingNs` 🆕                                     |
| `algorithms/stitching/phase_correlation/`                | `PhaseCorrelationNs` 🆕                              |
| `algorithms/stitching/overlap_estimation/`               | `OverlapEstimationNs` 🆕                             |
| `algorithms/stitching/sift/`                             | `SiftNs` 🆕                                          |
| `algorithms/stitching/lightglue/`                        | `LightGlueNs` 🆕                                     |
| `algorithms/stitching/positioning/`                      | `PositioningNs` 🆕                                   |
| `algorithms/rejection/` (interface `outlier_rejector.h`) | `RejectionNs` 🆕                                     |
| `algorithms/rejection/ransac/`                           | `RansacNs` 🆕                                        |
| `algorithms/brightness_equalization/`                    | `BrightnessEqualizationNs` 🆕                        |
| `algorithms/seamline/dynamic_programming_dijkstra/`      | `DynamicProgrammingDijkstraNs` 🆕 (ancien `seamlib`) |
| `algorithms/seamline/graph_cut/`                         | `GraphCutNs` 🆕 (ancien `Seamline`)                  |

**Le build compile tout seul tout ce qui est dans `src/backend/`.** Il le fait par `GLOB_RECURSE` ([cmake/backend.cmake](../cmake/backend.cmake)), sans liste de fichiers. Un `.cpp` posé ailleurs, par exemple dans `include/`, n'est **pas** compilé. Seule exception : les sources LightGlue sont retirées si ONNX Runtime ou le module `dnn` d'OpenCV manquent.

---

## 4. Arborescence annotée, fichier par fichier

### Racine et build

| Fichier                                                                 | Rôle                                                                                                                                                                                                                         |
| ----------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [CMakeLists.txt](../CMakeLists.txt)                                     | Point d'entrée, commenté ligne à ligne : préfixe Homebrew de Qt sous macOS, inclusion des fichiers de `cmake/` dans l'ordre, cible `stitching` (vide, nom provisoire).                                                       |
| [cmake/clang_tidy.cmake](../cmake/clang_tidy.cmake)                     | Option `ENABLE_CLANG_TIDY` (désactivée par défaut) : passe chaque fichier dans clang-tidy à la compilation. Règles dans [.clang-tidy](../.clang-tidy).                                                                       |
| [cmake/dependencies_manager.cmake](../cmake/dependencies_manager.cmake) | OpenCV, libtiff et **Qt 6 obligatoires** ; nlohmann_json téléchargé s'il manque.                                                                                                                                             |
| [cmake/backend.cmake](../cmake/backend.cmake)                           | Cible `kikirpa_backend` (lib statique de tout `src/backend`), détection d'ONNX Runtime (LightGlue facultatif), option `/utf-8` pour MSVC.                                                                                    |
| [cmake/frontend.cmake](../cmake/frontend.cmake)                         | Cible `kikirpa_frontend` (Qt).                                                                                                                                                                                               |
| [cmake/tests_runner.cmake](../cmake/tests_runner.cmake)                 | GoogleTest **obligatoire** (sauf `-DBUILD_TESTING=OFF`), un `add_subdirectory` par dossier de `tests/`. Liste des tests lue au lancement de `ctest` (sous Windows, les DLL d'OpenCV ne sont dans le PATH qu'à ce moment-là). |
| [cmake/install.cmake](../cmake/install.cmake)                           | Installe `stitching` dans `bin/`.                                                                                                                                                                                            |
| [CMakePresets.json](../CMakePresets.json)                               | Presets `windows-aqt` (Qt via aqt, libtiff et GoogleTest via vcpkg ; variables `KIKIRPA_VCPKG_ROOT`, `OpenCV_DIR` et `QT_ROOT`), `linux`, `macos` (Homebrew) et `linux-clang-tidy`. Build dans `build/<preset>/`.            |
| [README.md](../README.md)                                               | Présentation, build générique, renvoi vers [BUILD.md](BUILD.md) pour Windows.                                                                                                                                                |
| [.github/workflows/](../.github/workflows/)                             | `ci.yml` (build et tests à chaque push et PR, plus clang-tidy sur les fichiers C++ modifiés d'une PR), `release.yml` (tag `v*`), `build-test.yml` (job commun Ubuntu, installe Qt et GoogleTest).                            |
| [src/main.cpp](../src/main.cpp)                                         | Réservé au frontend : **vide** (`TODO`).                                                                                                                                                                                     |

### `core/` : le moteur

| Fichier                                                                                                                                   | Rôle                                                                                                                                                                                                                                                                                                                                                                                 |
| ----------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| [algorithm_types.h](../include/backend/core/data/algorithm_types.h)                                                                       | **Les types partagés** (`DataNs`) : `Direction`, `PairContext`, `OverlapEstimate`, `TileImage`, `Matches`, `PairMatches`, `Alignment`, `TilePositionData`, `TileTerritory` 🆕.                                                                                                                                                                                                       |
| [step.h](../include/backend/core/step/step.h) / [step.cpp](../src/backend/core/step/step.cpp)                                             | `StepClass`, classe abstraite d'une étape : `execute()`, sa config JSON (`configJson`), `getParameter("nom")`, `result` (résumé ou erreur), pointeur vers le `dataStore`. `importConfig()` fusionne et valide les paramètres du JSON ; `externalInputsFingerprint()` décrit ce que le step lit hors du pipeline.                                                                     |
| [pipeline.h](../include/backend/core/pipeline/pipeline.h) / [pipeline.cpp](../src/backend/core/pipeline/pipeline.cpp)                     | `PipelineClass` : liste ordonnée de steps (`addStep`). `configure(json)` applique un script, `exportConfig()` / `exportScript()` le réécrivent. `run()` les exécute dans l'ordre et s'arrête au premier échec ou à la première exception. `run()` sait aussi reprendre (`resolveStart`), faire une pause (`requestPause`) et réécrire le script après chaque step (`setScriptPath`). |
| [pipeline_builder.h](../include/backend/core/pipeline/pipeline_builder.h) / [.cpp](../src/backend/core/pipeline/pipeline_builder.cpp)     | **Registre des steps** (`stepRegistry()` : nom → classe), `createStep`, `getRegisteredStepNames`, `buildPipeline(json)` (validation + contrôle de l'enchaînement), `buildPipelineFromFile`.                                                                                                                                                                                          |
| [run_state.h](../include/backend/core/pipeline/run_state.h) / [.cpp](../src/backend/core/pipeline/run_state.cpp)                          | `RunState`, champ `"run"` du script : `status`, `completedSteps`, consignes `skipCompletedSteps` / `startFromStep`, `checkpointDirectory`. `RunState::fromJson` le valide.                                                                                                                                                                                                           |
| [checkpoint.h](../include/backend/core/checkpoint/checkpoint.h) / [.cpp](../src/backend/core/checkpoint/checkpoint.cpp)                   | `CheckpointClass` : sauvegarde des sorties de chaque step, un dossier `NN_nom/` par step, `manifest.json` écrit en dernier. Valide si même step, mêmes paramètres et même empreinte des entrées. `registerType` pour un nouveau type.                                                                                                                                                |
| [checkpoint_types.h](../include/backend/core/checkpoint/checkpoint_types.h) / [.cpp](../src/backend/core/checkpoint/checkpoint_types.cpp) | (Dé)sérialisation JSON des types du pipeline : `Tiles` (fiches seulement, sans pixels), `PairMatches`, `Positions`, `Territories`.                                                                                                                                                                                                                                                   |
| [data_store.h](../include/backend/core/step/data_store.h)                                                                                 | `DataStoreClass`, sac de données entre steps : `map<string, std::any>`. `add` refuse une clé qui existe, `replace` l'écrase (les steps utilisent `replace`). `get<T>` lève une exception si la clé manque ou si le type ne correspond pas.                                                                                                                                           |
| [pipeline_config.h](../include/backend/core/config/pipeline_config.h)                                                                     | Structures du script JSON (`StepParameter`, `DataSlot`, `StepConfig`, `PipelineConfig`) et leur (dé)sérialisation 🆕. Types de paramètre autorisés : `string`, `int`, `float`, `bool`.                                                                                                                                                                                               |
| [tile.h](../include/backend/core/data/tile.h) / [tile.cpp](../src/backend/core/data/tile.cpp)                                             | `TileClass`, fiche d'une tuile **sans pixels** : id, chemins (source et TIFF pyramidal), type OpenCV, 4 niveaux de pyramide (`LEVEL_COUNT`) en blocs de 512 (`BLOCK_SIZE`), voisines gauche/droite/haut/bas. Définit `TileList = vector<shared_ptr<TileClass>>`.                                                                                                                     |
| [pyramidal_tiff_writer.h](../include/backend/core/io/pyramidal_tiff_writer.h) / [.cpp](../src/backend/core/io/pyramidal_tiff_writer.cpp)  | Écrit un `cv::Mat` en TIFF tuilé 512 px, LZW, 4 niveaux (libtiff). Sert au cache d'import. ⚠️ BGR déclaré RGB, TIFF classique (pas BigTIFF).                                                                                                                                                                                                                                         |
| [pyramidal_tiff_reader.h](../include/backend/core/io/pyramidal_tiff_reader.h) / [.cpp](../src/backend/core/io/pyramidal_tiff_reader.cpp)  | `PyramidalTiffReaderClass`, lit une région d'un niveau du TIFF en ne chargeant que les tuiles intersectées et expose `readMetadata` pour le profil ICC et les résolutions TIFF optionnelles. Voir [PyramidalTiff.md](core/PyramidalTiff.md).                                                                                                                                         |

### `steps/` : les étapes du pipeline

| Fichier                                                                      | Nom JSON            | Lit                                                                                                                          | Écrit                                     | Appelle                                                                                                           |
| ---------------------------------------------------------------------------- | ------------------- | ---------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| [ImportStepClass](../src/backend/steps/import_step.cpp)                      | `import`            | paramètres `path`, `format` (regex col/ligne)                                                                                | `"tiles"` : `Tiles`                       | `cv::imread` + `createPyramidalTiledTiff` (cache dans `<images>/.cache/pyramids`)                                 |
| [PhaseCorrelationStepClass](../src/backend/steps/phase_correlation_step.cpp) | `phase-correlation` | `"tiles"` ; 12 paramètres, dont `niveau` (analyse sur un niveau réduit de la pyramide, résultat ramené en pleine résolution) | `"matches"` : `vector<PairMatches>`       | `PhaseCorrelationNs::PhaseCorrelationMatcherClass` sur chaque paire droite/dessous, pixels lus par `readPixels()` |
| [GraphCutStepClass](../src/backend/steps/graph_cut_step.cpp)                 | `graphcut`          | `"tiles"`, `"positions"` : `vector<TilePositionData>`                                                                        | `"territories"` : `vector<TileTerritory>` | `GraphCutNs::ImagePretreatmentClass` + `GraphCutNs::GraphCutSeamFinderClass`                                      |

Ces 3 steps sont dans le registre : on peut les mettre dans un script JSON. `graphcut` ne peut pas encore y être utilisé, car aucun step ne produit `positions`, et `buildPipeline` le refuse.

`ImportStepClass` fournit aussi `externalInputsFingerprint()` (nom, taille et date des images sources), pour périmer son checkpoint quand une image change.

Chaque step a deux modes :

- **dans un pipeline** : les données viennent du `DataStoreClass` ;
- **en appel direct** : on passe une struct `XxxInput` au constructeur. Utile pour tester une étape seule.

### `algorithms/stitching/` : appariement et positionnement 🆕

| Fichier                                                                                                                                                                                                              | Rôle                                                                                                                                                                                                                                                           |
| -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [feature_matcher.h](../include/backend/algorithms/stitching/feature_matcher.h)                                                                                                                                       | **Interface** de toute méthode d'appariement : `StitchingNs::FeatureMatcherClass::match(imageA, imageB, context, matches)`.                                                                                                                                    |
| **`phase_correlation/`** (`PhaseCorrelationNs`)                                                                                                                                                                      | Translation sous-pixel entre deux tuiles, en deux passes, plus des points raffinés localement. Port de `correlation_phase.py`. Voir §6.                                                                                                                        |
| ↳ [phase_correlation_matcher.h](../include/backend/algorithms/stitching/phase_correlation/phase_correlation_matcher.h) / [.cpp](../src/backend/algorithms/stitching/phase_correlation/phase_correlation_matcher.cpp) | Façade `PhaseCorrelationMatcherClass` : le seul header à inclure. `match()` enchaîne les 3 modules ; `getFailureReason()` explique un échec. Paramètres documentés en tête du header, avec leur nom dans le step.                                              |
| ↳ [phase_correlation_types.h](../include/backend/algorithms/stitching/phase_correlation/phase_correlation_types.h)                                                                                                   | Structures internes : `Parameters`, `OrientedPair`, `Strips`, `Measurement`, `Overlap`, `Correspondence`…                                                                                                                                                      |
| ↳ [preparation](../src/backend/algorithms/stitching/phase_correlation/preparation.cpp)                                                                                                                               | `PreparationClass` : tuiles brutes → images de travail (gris en `float64`, NaN = vide), paire ramenée à « B en bas ou à droite », découpe des bandes.                                                                                                          |
| ↳ [algorithm](../src/backend/algorithms/stitching/phase_correlation/algorithm.cpp)                                                                                                                                   | `AlgorithmClass` : corrélation de phase globale, affinage par DFT, diagnostic d'échec, points locaux.                                                                                                                                                          |
| ↳ [output](../src/backend/algorithms/stitching/phase_correlation/output.cpp)                                                                                                                                         | `OutputClass` : mise au format `DataNs::Matches` dans le repère d'origine, messages d'échec explicites (en anglais).                                                                                                                                           |
| **`overlap_estimation/`** [overlap_estimator](../src/backend/algorithms/stitching/overlap_estimation/overlap_estimator.cpp)                                                                                          | `OverlapEstimationNs::OverlapEstimatorClass` : 1re passe commune à toutes les méthodes, position de B au pixel près et recouvrement réel (`estimate()`). `computeCommonAreas()` donne les rectangles à découper dans A et B.                                   |
| **`sift/`** [sift_matcher](../src/backend/algorithms/stitching/sift/sift_matcher.cpp)                                                                                                                                | `SiftNs::SiftMatcherClass` : implémente `FeatureMatcherClass` et délègue à…                                                                                                                                                                                    |
| ↳ [sift_matcher_algorithm](../src/backend/algorithms/stitching/sift/sift_matcher_algorithm.cpp)                                                                                                                      | …`runSiftMatching()` : passage en 8 bits juste avant le détecteur, SIFT, BFMatcher + test de Lowe, translation médiane.                                                                                                                                        |
| **`lightglue/`** [lightglue_matcher](../src/backend/algorithms/stitching/lightglue/lightglue_matcher.cpp)                                                                                                            | `LightGlueNs::LightGlueMatcherClass` : appariement par réseau (ONNX Runtime), chargement du modèle, prétraitement (redimensionnement à un multiple de 32), inférence, remise à l'échelle. Compilé seulement si ONNX Runtime est trouvé (**jamais par la CI**). |
| **`positioning/`** [tile_positioner](../src/backend/algorithms/stitching/positioning/tile_positioner.cpp)                                                                                                            | `PositioningNs::computeTilePositions()` : à partir des **chemins**, refait recouvrement → matcher → rejector sur chaque paire, puis `solvePositions()` (moindres carrés globaux, positions et rotations). Écrit les résidus dans un `ostream`.                 |

### `algorithms/rejection/` : rejet des outliers 🆕

| Fichier                                                                                   | Rôle                                                                                                                                                                                                                            |
| ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [outlier_rejector.h](../include/backend/algorithms/rejection/outlier_rejector.h)          | **Interface** `RejectionNs::OutlierRejectorClass` : `estimate(matches, alignment)`.                                                                                                                                             |
| [ransac_rejector](../src/backend/algorithms/rejection/ransac/ransac_rejector.cpp)         | `RansacNs::RansacRejectorClass` : façade RANSAC (lit les paramètres JSON). |
| [vote_rejector](../src/backend/algorithms/rejection/vote_2d/vote_rejector.cpp)           | `RejectionNs::VoteRejectorClass` : vote de Hough 2D + médiane itérative (translation pure), utilisé par défaut par le positionnement. |
| [ransac_types.h](../include/backend/algorithms/rejection/ransac/ransac_types.h)           | `RansacParameters`, `RansacInput`, `RansacPreparedInput`, `RansacOutput`.                                                                                                                                                       |
| [ransac_preprocessor](../src/backend/algorithms/rejection/ransac/ransac_preprocessor.cpp) | `RansacPreprocessorClass` : contrôle des points (tailles, valeurs finies, minimum de matches).                                                                                                                                  |
| [ransac_algorithm](../src/backend/algorithms/rejection/ransac/ransac_algorithm.cpp)       | `RansacAlgorithmClass` : `cv::estimateAffinePartial2D` (similitude B → A), mise en 3×3, masque des inliers.                                                                                                                     |

### `algorithms/brightness_equalization/` : égalisation de luminosité 🆕

Port du notebook `egalisation_gain_champ_final.ipynb`, namespace `BrightnessEqualizationNs`. Même découpage que la corrélation de phase. Il n'y a pas encore de step : on l'appelle à la main.

| Fichier                                                                                                                                                                                     | Rôle                                                                                                                                                                                                                                                                                                       |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [brightness_equalizer.h](../include/backend/algorithms/brightness_equalization/brightness_equalizer.h) / [.cpp](../src/backend/algorithms/brightness_equalization/brightness_equalizer.cpp) | Façade `BrightnessEqualizerClass` : `reduceTile()` (une tuile à la fois), `estimate()` (gains et champ), `correctTile()` (tuile pleine résolution), `buildPairsFromPositions()`. Méthode A : un gain par tuile ; méthode B : gain + assombrissement vers les bords `exp(c·r²)` commun à toutes les tuiles. |
| [brightness_equalization_types.h](../include/backend/algorithms/brightness_equalization/brightness_equalization_types.h)                                                                    | `Parameters`, `ReducedTile`, `NeighbourPair`, `PairSamples`, `Solution`, `QualityReport`, `EqualizationResult`, `CorrectionMethod`.                                                                                                                                                                        |
| [preparation](../src/backend/algorithms/brightness_equalization/preparation.cpp)                                                                                                            | `PreparationClass` : tuiles 8 ou 16 bits, gris ou couleur → luminance réduite (1/8) et floutée.                                                                                                                                                                                                            |
| [algorithm](../src/backend/algorithms/brightness_equalization/algorithm.cpp)                                                                                                                | `AlgorithmClass` : échantillons dans les recouvrements, moindres carrés robustes (IRLS, Huber), bilan avant / A / B.                                                                                                                                                                                       |
| [output](../src/backend/algorithms/brightness_equalization/output.cpp)                                                                                                                      | `OutputClass` : correction pixel par pixel (sortie 16 bits), rapport lisible.                                                                                                                                                                                                                              |

### `algorithms/seamline/` : les coutures 🆕

**`dynamic_programming_dijkstra/`** (namespace `DynamicProgrammingDijkstraNs`, ancienne bibliothèque `seamlib`) : port du notebook _Projet_KIK_IRPA_Montauban_. Les en-têtes citent la section du notebook correspondante, et les commentaires gardent les noms Python d'origine (`solve_edge`, `w_color`…).

| Fichier                                                                                                                                                                                      | Rôle                                                                                                                                                                   |
| -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [seam_finder.h](../include/backend/algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h) / [.cpp](../src/backend/algorithms/seamline/dynamic_programming_dijkstra/seam_finder.cpp) | **Point d'entrée** : `SeamFinderClass::computeSeam(overlapLeft, overlapRight)`, soit carte de coût puis chemin. Travaille sur **un** overlap déjà découpé, en **RGB**. |
| [cost_map](../src/backend/algorithms/seamline/dynamic_programming_dijkstra/cost_map.cpp)                                                                                                     | Section 5 : gradients de Sobel, normalisations, termes couleur et gradient → carte de coût (`computeCostMaps`, `CostMapParameters`).                                   |
| [cost_catalog](../src/backend/algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.cpp)                                                                                             | Section 5.0 : catalogue des fonctions de coût (`CostFunction::ColorGradient`…, `computeCost`).                                                                         |
| [dynamic_programming](../src/backend/algorithms/seamline/dynamic_programming_dijkstra/dynamic_programming.cpp)                                                                               | Section 6.1 : chemin de coût minimal par programmation dynamique (`findDynamicProgrammingPath`, monotone, rapide).                                                     |
| [dijkstra](../src/backend/algorithms/seamline/dynamic_programming_dijkstra/dijkstra.cpp)                                                                                                     | Section 6.2 : Dijkstra 8 voisins exact (`findDijkstraPath`) et variante restreinte à un corridor autour du chemin DP.                                                  |
| [solver](../src/backend/algorithms/seamline/dynamic_programming_dijkstra/solver.cpp)                                                                                                         | Section 6.3 : `solveEdge()`, point d'entrée unique des 3 méthodes (`SolverMethod`), avec mesures de temps et de mémoire.                                               |
| [junctions](../src/backend/algorithms/seamline/dynamic_programming_dijkstra/junctions.cpp)                                                                                                   | Sections 7-8 : points de jonction entre 4 tuiles et réseau global de coutures sur la grille (`H_r_c` / `V_r_c`, `SeamGrid`, `solveGlobalNetwork`).                     |
| [overlay](../src/backend/algorithms/seamline/dynamic_programming_dijkstra/overlay.cpp)                                                                                                       | Dessin du réseau de coutures sur une mosaïque (`drawSeamNetwork`, illustration).                                                                                       |

**`graph_cut/`** (namespace `GraphCutNs`, ancien `Seamline`) : coupe de graphe d'OpenCV, paire par paire.

| Fichier                                                                                         | Rôle                                                                                                                                                                                                                                                                  |
| ----------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [image_pretreatment](../src/backend/algorithms/seamline/graph_cut/image_pretreatment.cpp)       | `ImagePretreatmentClass` : relit les tuiles depuis le disque (`read16Bit`, `readSmallFloat`), calcule les coins et les masques réduits. `parseTileName()` extrait ligne et colonne de `..._rLL_cCC.tif`. **Aussi utilisé par le positionnement.**                     |
| [graph_cut_seam_finder](../src/backend/algorithms/seamline/graph_cut/graph_cut_seam_finder.cpp) | `GraphCutSeamFinderClass` : `computeSeamline()` (coupe à échelle réduite, carte de provenance pleine résolution, mosaïque 8 bits d'aperçu), `extractPolygons()` (territoires par tuile, `DataNs::TileTerritory`), `verifyTiling()` (contrôle du pavage, `SeamCheck`). |

### `frontend/` : interface graphique

| Fichier                                                                                         | Rôle                                                                                                                                                                                      |
| ----------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [PipelineJson.h](../include/frontend/PipelineJson.h) / [.cpp](../src/frontend/PipelineJson.cpp) | Fabrique le JSON du pipeline à partir de champs de formulaire (`makeStep`, `parameterFromText`, `writePipelineJson`), namespace `FrontendNs`. La vraie GUI est sur la branche `frontend`. |

### `tests/`

Tous en GoogleTest, avec le label `unit`, et lancés par `ctest`. Il n'y a plus de test d'intégration ni d'outil manuel sur images réelles (décision du 02/11/2026 : uniquement des tests unitaires).

| Dossier                                                          | Tests                                    | Ce qu'ils vérifient                                                                                                                                                                                                                                                                                                                |
| ---------------------------------------------------------------- | ---------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [core/](../tests/core/)                                          | `PipelineBuilder.*` (5), `Reprise.*` (5) | Registre, validation du script, fusion des options, aller-retour `exportConfig` → `buildPipeline`, pipeline réel import → phase-correlation sur deux tuiles 16 bits générées, reprise sur checkpoints (échec, paramètres modifiés, `startFromStep`, `skipCompletedSteps`, pause, `run.json`), aller-retour des 4 types sérialisés. |
| [brightness_equalization/](../tests/brightness_equalization/) 🆕 | `BrightnessEqualization.*` (3)           | 6 tuiles générées avec gains et champ connus, gris et couleur, 8 et 16 bits ; cas limites.                                                                                                                                                                                                                                         |
| [phase_correlation/](../tests/phase_correlation/) 🆕             | `PhaseCorrelation.*` (3)                 | Paires générées à vérité sous-pixel exacte dans les 4 directions, les deux passes, les cas limites.                                                                                                                                                                                                                                |
| [seamline/](../tests/seamline/)                                  | 71 tests                                 | `DynamicProgrammingDijkstraNs` : carte de coût, catalogue, DP, Dijkstra, solveur, jonctions, parité avec le notebook.                                                                                                                                                                                                              |

Pour ajouter des tests : un dossier `tests/<module>/` avec son `CMakeLists.txt` (modèle : [tests/brightness_equalization/CMakeLists.txt](../tests/brightness_equalization/CMakeLists.txt)), et une ligne `add_subdirectory` dans [cmake/tests_runner.cmake](../cmake/tests_runner.cmake) ; l'exécutable de test se lie à `kikirpa_backend`.

### `docs/`

| Fichier                                                                                      | Contenu                                                                                                                                |
| -------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| [PhaseCorrelation.md](algorithms/stitching/phase_correlation/PhaseCorrelation.md) 🆕         | Méthode, paramètres (avec leur nom dans le step), limites et résultats chiffrés comparés au Python. **La meilleure doc du projet.**    |
| [OverlapEstimation.md](algorithms/stitching/overlap_estimation/OverlapEstimation.md) 🆕      | La 1re passe commune.                                                                                                                  |
| [BrightnessEqualization.md](algorithms/brightness_equalization/BrightnessEqualization.md) 🆕 | Modèle, méthodes A et B, paramètres, vérification.                                                                                     |
| [Pipeline.md](core/Pipeline.md)                                                              | Le pipeline : format du script, contrôles, exécution, reprise et checkpoints, steps enregistrés, ajout d'un step.                      |
| [PhaseCorrelationStep.md](steps/PhaseCorrelationStep.md)                                     | Contrat du step `phase-correlation` : paramètres, conventions d'axes et de signe, résolution réduite, échecs.                          |
| [BUILD.md](BUILD.md)                                                                         | Installer les dépendances et compiler, sous Windows (vcpkg + Qt via aqt), macOS (Homebrew) et Linux.                                   |
| [CONVENTIONS.md](CONVENTIONS.md) 🆕                                                          | Règles de code (noms, mise en forme, includes, erreurs, classes et fonctions, tests), principes de code propre, Git et pull requests.  |
| [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md) 🆕                                | Ce que la réorganisation de `core/` et `steps/`, puis la migration des algorithmes (§12), ont renommé, et comment adapter une branche. |
| cette carte                                                                                  | Se repérer dans le code.                                                                                                               |

---

## 5. Les données qui circulent

Les types sont dans [algorithm_types.h](../include/backend/core/data/algorithm_types.h) (namespace `DataNs`), sauf `TileList` ([tile.h](../include/backend/core/data/tile.h)). `TileTerritory` y est aussi 🆕 (l'alias `Seamline::TileTerritory` a été supprimé).

```txt
fichiers .tif ──ImportStepClass──► "tiles" : TileList = vector<shared_ptr<TileClass>>   (fiches, sans pixels)
                                  │
                PhaseCorrelationStepClass (relit les .tif sources, éventuellement réduits)
                                  ▼
                        "matches" : vector<PairMatches>
                                  │       PairMatches = { tileIdA, tileIdB, direction, featureMatches }
                                  │       Matches     = { pointsInA[], pointsInB[], confidenceScores[],
                                  │                       translationX, translationY }
                                  ▼
                  (RANSAC → moindres carrés : pas encore de step)
                                  ▼
                       "positions" : vector<TilePositionData>   { tileId "rLL_cCC", position, rotationRadians }
                                  │
                  (égalisation : algorithme prêt, pas encore de step)
                                  │
                GraphCutStepClass (relit les .tif sources)
                                  ▼
                       "territories" : vector<TileTerritory>    { tileIndex, polygons, holes } par tuile
                                  ▼
                       (assemblage → export : à écrire)
```

**Un type C++ par clé** : le registre vérifie l'enchaînement d'après le _nom_ de type déclaré (`"Tiles"`, `"PairMatches"`…). `DataStoreClass::get<T>` exige, lui, le _type C++_ exact. Un nouveau step doit donc réutiliser ces types, sinon `std::bad_any_cast` à l'exécution. Ce sont aussi ces 4 types que les checkpoints savent sérialiser :

| Nom (JSON)    | Clé           | Type C++                                             |
| ------------- | ------------- | ---------------------------------------------------- |
| `Tiles`       | `tiles`       | `DataNs::TileList` = `vector<shared_ptr<TileClass>>` |
| `PairMatches` | `matches`     | `vector<DataNs::PairMatches>`                        |
| `Positions`   | `positions`   | `vector<DataNs::TilePositionData>`                   |
| `Territories` | `territories` | `vector<DataNs::TileTerritory>`                      |

**Les conventions de repère à connaître :**

- **`Direction`** donne la position de B par rapport à A (`Top`, `Bottom`, `Left`, `Right`). Les anciens alias `LeftRight` et `BottomRight` ont disparu 🆕.
- **`Matches::translationX, translationY`** donnent le coin haut-gauche de B dans le repère de A, soit `pointInA − pointInB`. Toujours en pixels de pleine résolution, même quand `phase-correlation` mesure sur un niveau réduit.
- **`Alignment::transformBToA`** est une matrice 3×3 `CV_64F` qui transforme B vers A.
- **`cv::Point` vaut (x, y) = (colonne, ligne).** Le Python de référence stocke (y, x).
- **L'égalisation** identifie les tuiles par leur **indice** dans un vecteur, pas par `tileId`, et prend la translation d'une paire telle quelle.

---

## 6. Lire un algorithme : l'exemple de la corrélation de phase

C'est le modèle de découpage que tout nouvel algorithme doit suivre (voir [CONVENTIONS.md](CONVENTIONS.md)). Ordre de lecture conseillé :

1. **[phase_correlation_matcher.h](../include/backend/algorithms/stitching/phase_correlation/phase_correlation_matcher.h)** : ce que fait l'algorithme et ses paramètres, documentés dans l'en-tête.
2. **[phase_correlation_matcher.cpp](../src/backend/algorithms/stitching/phase_correlation/phase_correlation_matcher.cpp)** : l'orchestrateur (`match()`), court. Il dit dans quel ordre les modules sont appelés.
3. **[phase_correlation_types.h](../include/backend/algorithms/stitching/phase_correlation/phase_correlation_types.h)** : les structures échangées entre modules.
4. Les trois modules dans l'ordre d'exécution : **PreparationClass → AlgorithmClass → OutputClass**. Chaque header liste les fonctions publiques, avec un commentaire qui dit _quoi_ ; les fonctions internes sont dans un `namespace { }` anonyme du `.cpp`, qui dit _pourquoi_.
5. **[PhaseCorrelation.md](algorithms/stitching/phase_correlation/PhaseCorrelation.md)** pour la théorie et les résultats.
6. **[test_phase_correlation.cpp](../tests/phase_correlation/test_phase_correlation.cpp)** pour voir l'algorithme utilisé.

L'**égalisation** suit exactement le même schéma (BrightnessEqualizerClass → PreparationClass → AlgorithmClass → OutputClass), et RANSAC à peu près (RansacPreprocessorClass → RansacAlgorithmClass → RansacRejectorClass). SIFT, LightGlue, `DynamicProgrammingDijkstraNs` et GraphCut ne le suivent pas encore.

---

## 7. Pièges de lecture

| Ce qu'on pourrait croire                                                             | La réalité                                                                                                                                                                                                                                                                                                                         |
| ------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `main.cpp` lance le pipeline                                                         | Il est vide (réservé au frontend). Il n'y a **plus de ligne de commande** (`stitching_run` a été retiré par #43) : le pipeline se lance depuis le code.                                                                                                                                                                            |
| Les `inputs` et `outputs` du JSON définissent les entrées et sorties                 | Ils sont ignorés à la lecture : c'est le constructeur du step qui les fixe. Seules les **valeurs** des paramètres sont reprises du JSON.                                                                                                                                                                                           |
| Un ancien script JSON (`options`, `type`, `key`) se relit                            | Non 🆕 : le format a changé avec #50 (`parameters`, `valueType`, `name`). Les anciens checkpoints sont jugés périmés et refaits.                                                                                                                                                                                                   |
| Les checkpoints sont actifs par défaut                                               | Non : `checkpointDirectory` est vide par défaut. Il faut le remplir dans `RunState` (champ `run.checkpointDirectory` du script).                                                                                                                                                                                                   |
| Un checkpoint de `tiles` contient les images                                         | Non : seulement les fiches. Les pixels restent dans les TIFF source et pyramidaux, qui doivent toujours exister à la reprise.                                                                                                                                                                                                      |
| Les steps lisent les pixels via `TileClass`                                          | `PyramidalTiffReaderClass::readRegion` existe pour lire une région d'un TIFF pyramidal, mais les steps ne l'utilisent pas encore : `PhaseCorrelationStepClass` relit actuellement les `.tif` sources avec `cv::imread` dans `readPixels()`.                                                                                        |
| `computeTilePositions` prend les matches du step d'appariement                       | Non : il refait tout lui-même à partir des chemins de fichiers.                                                                                                                                                                                                                                                                    |
| `ransac_rejector.cpp` contient tout le rejet                                         | Le rejet par vote de Hough 2D (`VoteRejectorClass`), utilisé par défaut, est implémenté dans son module dédié `vote_rejector.cpp`. |
| `ImagePretreatmentClass` ne sert qu'à GraphCut                                       | Le positionnement l'utilise aussi (`read16Bit`, `parseTileName`).                                                                                                                                                                                                                                                                  |
| Les tuiles sont en RGB                                                               | OpenCV donne du **BGR**. `DynamicProgrammingDijkstraNs` attend du **RGB**.                                                                                                                                                                                                                                                         |
| Les champs de `PhaseCorrelationNs::Parameters` portent le nom des paramètres du step | Non 🆕 : les champs sont en anglais (`smoothingSigma`, `railMargin`…), le JSON garde `lissage`, `marge`… (correspondance dans [PhaseCorrelation.md §5](algorithms/stitching/phase_correlation/PhaseCorrelation.md#5-paramètres)).                                                                                                  |
| Le TIFF pyramidal est la sortie du pipeline                                          | C'est un cache d'import, par tuile. Il n'y a pas encore d'export.                                                                                                                                                                                                                                                                  |
| Il existe un exemple de la chaîne complète (import → couture → mosaïque)             | Plus sur `main` : `test_chaine_paire` a été supprimé avec les tests d'intégration (#43). Il reste dans l'historique : `git show c383a38:tests/algorithms/integration/test_chaine_paire.cpp`.                                                                                                                                       |
| Qt est facultatif                                                                    | Plus depuis #43 : sans Qt 6 ni GoogleTest, `cmake` échoue. Sous Windows, Qt s'installe avec aqt et GoogleTest avec vcpkg ([BUILD.md](BUILD.md)).                                                                                                                                                                                   |
| Tout le code suit déjà les conventions                                               | Presque 🆕 : `core/`, `steps/`, `frontend/` et `algorithms/` ont été migrés. Restent les noms internes des tests (français, ou `snake_case` dans `tests/seamline/`), quelques sorties console et les noms des paramètres JSON gardés pour compatibilité : voir [CONVENTIONS.md §9](CONVENTIONS.md#9-ce-qui-nest-pas-encore-migré). |
| `docs/` couvre tous les algorithmes                                                  | Seulement la corrélation de phase, le recouvrement et l'égalisation, plus le pipeline, le step `phase-correlation` et le build.                                                                                                                                                                                                    |

---

## 8. Ce qui n'existe qu'en branche

État au 03/11/2026. Pour savoir où chercher quand un fichier n'est pas sur `main`. **Toutes ces branches datent d'avant la réorganisation (#50)** : à leur prochaine mise à jour, elles doivent suivre [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md) §10, et §12 si elles touchent aux algorithmes.

| Je cherche…                                             | Branche (PR)                                                     | État                                                                                                                                                 |
| ------------------------------------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| Le step SIFT                                            | `sift-step` (#32)                                                | Changements demandés : registre, conventions, code commun avec LightGlue. Déclare encore ses tests dans `cmake/algorithms.cmake`, qui n'existe plus. |
| Le step LightGlue                                       | `lightglue-step` (#27)                                           | Changements demandés : découpe du recouvrement, registre, conventions. Ajoute `steps/MatchingStepUtils.h` (code commun aux steps d'appariement).     |
| Indicateur de confiance de la corrélation de phase      | `confiance-correlation-phase` (#48)                              | Ouverte. Étude dans `docs/.../EtudeConfiance.md` ; déclare encore ses tests dans `cmake/algorithms.cmake`.                                           |
| Corrections GraphCut (fautes, place de `TileTerritory`) | `GraphCut_Fixed` (#49)                                           | Ouverte. Déplace `TileTerritory` vers `core/Types.h`, ce que #50 a déjà fait (`DataNs::TileTerritory`).                                              |
| Le step RANSAC                                          | `RansacStep`                                                     | Pas de PR. `include/Backend/steps/RansacStep.{h,cpp}` (ancienne arborescence) (⚠️ `.cpp` mal placé, donc pas compilé).                               |
| Squelettes des steps fusion et export, `CONTRAT.md`     | `fe/pipeline-json-parser` (#47, et #41 vers `pipeline_pr_patch`) | Architecture parallèle à celle de `main` (types différents, pixels en mémoire).                                                                      |
| La vraie GUI                                            | `frontend` (#9, brouillon)                                       | ⚠️ Contient des images de test `.tif` (`datasetdetest/`), qui alourdiraient l'historique.                                                            |
| Le step d'intensité                                     | `amo/IntensityCorrectionStep`                                    | Branche créée, encore vide (identique à un ancien `main`).                                                                                           |

Pour voir un fichier d'une branche sans changer de branche :

```bash
git show origin/RansacStep:include/Backend/steps/RansacStep.h
```
