# Carte de l'architecture : KIKIRPA-CPP

Guide pour se repérer dans le code de **`main`**, à jour du 07/11/2026 (`22f371d`, après #80). Depuis la migration des algorithmes (#76), main a reçu la lecture par région (#61), les steps LightGlue (#27), SIFT (#32), `absolute-pos` (#77) et `intensity-correction` (#80), l'indicateur de confiance de la corrélation de phase (#78), le nouveau rejet par vote 2D (#63, qui supprime RANSAC), la mesure de mémoire (#93) et le nettoyage du solveur seamline (#66, #68). Les éléments marqués **🆕** ont changé avec la réorganisation de `core/` et `steps/` (PR #50) ou la migration des algorithmes (#76) : dossiers en minuscules, fichiers en `snake_case`, noms anglais, un namespace par dossier, nouveau format JSON. Correspondance complète entre anciens et nouveaux noms : [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md) (§12 pour les algorithmes). Règles de nommage : [CONVENTIONS.md](CONVENTIONS.md). La documentation a été complétée le 08/11/2026 (#96, #97), sans changement du code : guide [COMPRENDRE_LE_PROJET.md](COMPRENDRE_LE_PROJET.md), entrées et sorties dans chaque fiche de [steps/](steps/), marche à suivre [AJOUTER_UN_STEP.md](steps/AJOUTER_UN_STEP.md). Le 09/11, la PR #72 remplace la version de `intensity-correction` de #80 et ajoute le step `dynamic-programming-dijkstra`.

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

Le projet a trois couches. Les **algorithmes** sont les plus fournis. L'**orchestration** est en place : un script JSON se construit, se valide, s'exécute et se reprend. Il manque les steps de fin de chaîne (assemblage, export), et un point d'entrée pour l'utilisateur : ni ligne de commande ni interface sur `main`.

```txt
┌────────────────────────────────────────────────────────────────────────────┐
│ ENTRÉES              src/main.cpp (vide)  ·  src/frontend/ (PipelineJson)  │
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
    I[Import<br/>✅ import] --> R[Recouvrement<br/>algo ✅ · step ❌ PR #90]
    R --> M[Appariement<br/>✅ phase-correlation, sift, lightglue]
    M --> X[Rejet des aberrants<br/>algo ✅ vote 2D · step ❌]
    X --> L[Moindres carrés<br/>✅ absolute-pos]
    L --> E[Intensité<br/>✅ intensity-correction]
    E --> S[Seamline<br/>✅ graphcut · ✅ dynamic-programming-dijkstra]
    S --> A[Assemblage<br/>❌ PR #69]
    A --> O[Export<br/>❌ branche fe/export]
```

Avec les steps actuels, un script peut aller de l'import aux territoires de couture : `import` → un step d'appariement → `absolute-pos` → `graphcut`, sans rejet des aberrants. `intensity-correction` calcule les gains, mais aucun step ne les applique encore.

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
| **Comprendre le projet dans son ensemble**                                  | [COMPRENDRE_LE_PROJET.md](COMPRENDRE_LE_PROJET.md) : les étapes et leur état, comment lancer le pipeline, ce que chaque step reçoit et produit, la mémoire, ce qui reste à faire |
| **Compiler le projet** (Windows, Linux ou macOS)                            | [BUILD.md](BUILD.md) (Qt 6 et GoogleTest obligatoires ; sous Windows, Qt binaire via aqt, libtiff et GoogleTest via vcpkg) + [CMakePresets.json](../CMakePresets.json) (`cmake --preset windows-aqt`)                                     |
| **Lancer les tests**                                                        | `ctest --preset windows-aqt` (ou `linux`, `macos`) : environ 150 tests GoogleTest `unit`, plus `lightglue_model` (label `integration`, vrai modèle ONNX téléchargé à la configuration) ; `ctest -L unit` ne lance que les premiers |
| **Lancer un pipeline**                                                      | depuis le code : `buildPipelineFromFile()` puis `run()`, voir [pipeline.md](core/pipeline.md#lancer-un-pipeline). Exemple qui tourne : `testerPipelineReel()` dans [test_pipeline.cpp](../tests/core/test_pipeline.cpp)                   |
| **Comprendre le pipeline en une page**                                      | [pipeline.md](core/pipeline.md) : format du script, contrôles, reprise, steps enregistrés |
| **Savoir ce qu'un step lit et écrit**                                       | la section « Entrées et sorties » de sa fiche ; index : [steps/README.md](steps/README.md) |
| Voir comment un script JSON devient un pipeline                             | [pipeline_builder.cpp](../src/backend/core/pipeline/pipeline_builder.cpp) : `stepRegistry()`, `buildPipeline()`                                                                                                                           |
| **Ajouter un step au pipeline**                                             | [AJOUTER_UN_STEP.md](steps/AJOUTER_UN_STEP.md) : exemple complet (en-tête, constructeur, `execute()`, une ligne dans `stepRegistry()` de [pipeline_builder.cpp](../src/backend/core/pipeline/pipeline_builder.cpp), checkpoint d'un nouveau type, test, fiche) |
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
| **Écrire un nouveau step** : modèle à copier                                | l'exemple de [AJOUTER_UN_STEP.md](steps/AJOUTER_UN_STEP.md) ; dans le code, [absolute_pos_step.cpp](../src/backend/steps/absolute_pos_step.cpp) ([AbsolutePosStep.md](steps/AbsolutePosStep.md)) pour un step court, [phase_correlation_step.cpp](../src/backend/steps/phase_correlation_step.cpp) ([PhaseCorrelationStep.md](steps/PhaseCorrelationStep.md)) pour un step qui lit des images |
| Écrire un step d'appariement (voisines, lecture, recalage)                  | [matching_step_utils.h](../include/backend/steps/matching_step_utils.h) (code commun à `sift` et `lightglue`)                                                                                                                             |
| Voir les steps SIFT et LightGlue                                            | [sift_step.cpp](../src/backend/steps/sift_step.cpp) + [SiftStep.md](steps/SiftStep.md) ; [lightglue_step.cpp](../src/backend/steps/lightglue_step.cpp) + [LightGlueStep.md](steps/LightGlueStep.md)                                      |
| **Passer des appariements aux positions globales**                          | [absolute_pos_step.cpp](../src/backend/steps/absolute_pos_step.cpp) (step `absolute-pos`) → [position_solver.h](../include/backend/algorithms/stitching/positioning/position_solver.h) (`solveRelativePositions`, SVD)                  |
| **Estimer les gains de luminosité dans le pipeline**                        | [intensity_correction_step.cpp](../src/backend/steps/intensity_correction_step.cpp) + [IntensityCorrectionStep.md](steps/IntensityCorrectionStep.md) ; type `IntensityCorrection` dans [algorithm_types.h](../include/backend/core/data/algorithm_types.h) |
| Mesurer la mémoire consommée par un bloc de code                            | [memory_scope.h](../include/backend/core/memory/memory_scope.h) (`MemoryNs::MemoryScopeClass`, écrit dans un JSON ; utilisé par aucun step pour l'instant)                                                                               |
| Comprendre l'indicateur de confiance de la corrélation de phase             | [EtudeConfiance.md](algorithms/stitching/phase_correlation/EtudeConfiance.md) ; contrôles dans [phase_correlation_step.h](../include/backend/steps/phase_correlation_step.h) (`getResult()["confidence"]`)                               |
| Mesurer le décalage entre deux tuiles (corrélation de phase)                | [phase_correlation_matcher.h](../include/backend/algorithms/stitching/phase_correlation/phase_correlation_matcher.h) + sa [doc](algorithms/stitching/phase_correlation/PhaseCorrelation.md) 🆕                                            |
| Estimer le recouvrement réel d'une paire (1re passe)                        | [overlap_estimator.h](../include/backend/algorithms/stitching/overlap_estimation/overlap_estimator.h) + sa [doc](algorithms/stitching/overlap_estimation/OverlapEstimation.md) 🆕                                                         |
| Voir l'appariement SIFT                                                     | [sift_matcher_algorithm.cpp](../src/backend/algorithms/stitching/sift/sift_matcher_algorithm.cpp) (le calcul) ; [sift_matcher.cpp](../src/backend/algorithms/stitching/sift/sift_matcher.cpp) (l'emballage) 🆕                            |
| Voir l'appariement LightGlue (ONNX)                                         | [lightglue_matcher.cpp](../src/backend/algorithms/stitching/lightglue/lightglue_matcher.cpp) 🆕                                                                                                                                           |
| Voir le rejet des mauvais points (vote de Hough 2D)                         | [vote_rejector.cpp](../src/backend/algorithms/rejection/vote_2d/vote_rejector.cpp) + sa [doc](algorithms/rejection/vote_2d/Vote2D.md) (seule méthode de rejet depuis #63)                                                               |
| **Voir l'alignement global** (moindres carrés)                              | [position_solver.cpp](../src/backend/algorithms/stitching/positioning/position_solver.cpp) : `solveRelativePositions()`, partagé par `absolute-pos` et `computeTilePositions`                                                           |
| **Égaliser la luminosité entre tuiles**                                     | [brightness_equalizer.h](../include/backend/algorithms/brightness_equalization/brightness_equalizer.h) + sa [doc](algorithms/brightness_equalization/BrightnessEqualization.md) 🆕                                                        |
| Voir le calcul des coutures, méthode DP/Dijkstra                            | [seam_finder.h](../include/backend/algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h) (point d'entrée) puis [solver.h](../include/backend/algorithms/seamline/dynamic_programming_dijkstra/solver.h) 🆕                      |
| Voir le calcul des coutures, méthode GraphCut                               | [graph_cut_seam_finder.cpp](../src/backend/algorithms/seamline/graph_cut/graph_cut_seam_finder.cpp) + [graph_cut_step.cpp](../src/backend/steps/graph_cut_step.cpp) 🆕                                                                    |
| Voir un vrai pipeline tourner depuis un script JSON                         | [test_pipeline.cpp](../tests/core/test_pipeline.cpp) : `testerPipelineReel()` (import → phase-correlation) et `testerRepriseReelle()`                                                                                                     |
| Générer le JSON du pipeline depuis la GUI                                   | [PipelineJson.h](../include/frontend/PipelineJson.h)                                                                                                                                                                                      |
| Voir comment le projet est compilé                                          | [CMakeLists.txt](../CMakeLists.txt) + [cmake/](../cmake/) (`clang_tidy`, `dependencies_manager`, `backend`, `frontend`, `tests_runner`, `install`)                                                                                        |
| **Nommer un fichier, une classe, une variable** 🆕                          | [CONVENTIONS.md](CONVENTIONS.md)                                                                                                                                                                                                          |
| Adapter une branche écrite avant la réorganisation 🆕                       | [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md) (§10 ; §12 pour les algorithmes)                                                                                                                                               |
| Voir ce que fait la CI                                                      | [build-test.yml](../.github/workflows/build-test.yml) (Ubuntu) et [build-test-windows.yml](../.github/workflows/build-test-windows.yml) (MSVC), appelés par `ci.yml` et `release.yml`                                                     |

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
        io/             écriture et lecture du TIFF pyramidal
        memory/         mesure de la mémoire consommée
    steps/          les étapes concrètes, une classe par étape (<nom>_step.h) 🆕
    algorithms/     les méthodes de calcul, un dossier et un namespace par méthode 🆕
        stitching/      appariement + recouvrement + positionnement global
        rejection/      rejet des outliers
        brightness_equalization/   égalisation de luminosité 🆕
        seamline/       calcul des coutures
include/frontend/ ⟷  src/frontend/   GUI Qt (quasi vide sur main : PipelineJson seulement)
tests/               un dossier par module, en GoogleTest : brightness_equalization/, core/, intensity_correction/, phase_correlation/, seamline/, steps/
docs/                COMPRENDRE_LE_PROJET.md (à lire en premier), algorithms/ (une page par algorithme), core/ (le pipeline),
                     steps/ (entrées et sorties de chaque step, AJOUTER_UN_STEP.md),
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
| `core/memory/`                                           | `MemoryNs`                                           |
| `steps/`                                                 | `StepsNs` 🆕                                         |
| `frontend/`                                              | `FrontendNs` 🆕                                      |
| `algorithms/stitching/` (interface `feature_matcher.h`)  | `StitchingNs` 🆕                                     |
| `algorithms/stitching/phase_correlation/`                | `PhaseCorrelationNs` 🆕                              |
| `algorithms/stitching/overlap_estimation/`               | `OverlapEstimationNs` 🆕                             |
| `algorithms/stitching/sift/`                             | `SiftNs` 🆕                                          |
| `algorithms/stitching/lightglue/`                        | `LightGlueNs` 🆕                                     |
| `algorithms/stitching/positioning/`                      | `PositioningNs` 🆕                                   |
| `algorithms/rejection/` (interface `outlier_rejector.h`) | `RejectionNs` 🆕                                     |
| `algorithms/rejection/vote_2d/`                          | `stitching::rejection` (ancien nom gardé, `NOLINT`), avec des alias dans `RejectionNs` |
| `algorithms/brightness_equalization/`                    | `BrightnessEqualizationNs` 🆕                        |
| `algorithms/seamline/dynamic_programming_dijkstra/`      | `DynamicProgrammingDijkstraNs` 🆕 (ancien `seamlib`) |
| `algorithms/seamline/graph_cut/`                         | `GraphCutNs` 🆕 (ancien `Seamline`)                  |

**Le build compile tout seul tout ce qui est dans `src/backend/`.** Il le fait par `GLOB_RECURSE` ([cmake/backend.cmake](../cmake/backend.cmake)), sans liste de fichiers. Un `.cpp` posé ailleurs, par exemple dans `include/`, n'est **pas** compilé. LightGlue est toujours compilé : ONNX Runtime est téléchargé à la configuration s'il n'est pas fourni (`-DONNXRUNTIME_ROOT`). Sous Windows, chaque exécutable de test doit appeler `kikirpa_copy_onnxruntime_dlls(<cible>)` pour trouver les DLL.

---

## 4. Arborescence annotée, fichier par fichier

### Racine et build

| Fichier                                                                 | Rôle                                                                                                                                                                                                                         |
| ----------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [CMakeLists.txt](../CMakeLists.txt)                                     | Point d'entrée, commenté ligne à ligne : préfixe Homebrew de Qt sous macOS, inclusion des fichiers de `cmake/` dans l'ordre, cible `stitching` (vide, nom provisoire).                                                       |
| [cmake/clang_tidy.cmake](../cmake/clang_tidy.cmake)                     | Option `ENABLE_CLANG_TIDY` (désactivée par défaut) : passe chaque fichier dans clang-tidy à la compilation. Règles dans [.clang-tidy](../.clang-tidy).                                                                       |
| [cmake/dependencies_manager.cmake](../cmake/dependencies_manager.cmake) | OpenCV, libtiff et **Qt 6 obligatoires** ; nlohmann_json et ONNX Runtime 1.20.1 téléchargés s'ils ne sont pas fournis.                                                                                                       |
| [cmake/backend.cmake](../cmake/backend.cmake)                           | Cible `kikirpa_backend` (lib statique de tout `src/backend`), liaison à ONNX Runtime et `opencv_dnn`, fonction `kikirpa_copy_onnxruntime_dlls`, option `/utf-8` pour MSVC.                                                  |
| [cmake/frontend.cmake](../cmake/frontend.cmake)                         | Cible `kikirpa_frontend` (Qt).                                                                                                                                                                                               |
| [cmake/tests_runner.cmake](../cmake/tests_runner.cmake)                 | GoogleTest **obligatoire** (sauf `-DBUILD_TESTING=OFF`), un `add_subdirectory` par dossier de `tests/`. Liste des tests lue au lancement de `ctest` (sous Windows, les DLL d'OpenCV ne sont dans le PATH qu'à ce moment-là). |
| [cmake/install.cmake](../cmake/install.cmake)                           | Installe `stitching` dans `bin/`.                                                                                                                                                                                            |
| [CMakePresets.json](../CMakePresets.json)                               | Presets `windows-aqt` (Qt via aqt, libtiff et GoogleTest via vcpkg ; variables `KIKIRPA_VCPKG_ROOT`, `OpenCV_DIR` et `QT_ROOT`), `linux`, `macos` (Homebrew) et `linux-clang-tidy`. Build dans `build/<preset>/`.            |
| [README.md](../README.md)                                               | Présentation, état du projet, index de la documentation, compilation en bref et organisation du dépôt. |
| [.github/workflows/](../.github/workflows/)                             | `ci.yml` (build et tests à chaque push et PR, plus clang-tidy sur les fichiers C++ modifiés d'une PR), `release.yml` (tag `v*`, publie des `.zip` de binaires), `build-test.yml` (job commun Ubuntu) et `build-test-windows.yml` (job MSVC). |
| [src/main.cpp](../src/main.cpp)                                         | Réservé au frontend : **vide** (`TODO`).                                                                                                                                                                                     |

### `core/` : le moteur

| Fichier                                                                                                                                   | Rôle                                                                                                                                                                                                                                                                                                                                                                                 |
| ----------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| [algorithm_types.h](../include/backend/core/data/algorithm_types.h)                                                                       | **Les types partagés** (`DataNs`) : `Direction`, `PairContext`, `OverlapEstimate`, `TileImage`, `Matches`, `PairMatches`, `Alignment`, `TilePositionData`, `TileTerritory` 🆕.                                                                                                                                                                                                       |
| [verification_report.h](../include/backend/core/data/verification_report.h)                                                               | `DataNs::VerificationReport` : rapport JSON de dimensions et métadonnées de la vérification finale.                                                                                                                                                                                                                                                                              |
| [step.h](../include/backend/core/step/step.h) / [step.cpp](../src/backend/core/step/step.cpp)                                             | `StepClass`, classe abstraite d'une étape : `execute()`, sa config JSON (`configJson`), `getParameter("nom")`, `result` (résumé ou erreur), pointeur vers le `dataStore`. `importConfig()` fusionne et valide les paramètres du JSON ; `externalInputsFingerprint()` décrit ce que le step lit hors du pipeline.                                                                     |
| [pipeline.h](../include/backend/core/pipeline/pipeline.h) / [pipeline.cpp](../src/backend/core/pipeline/pipeline.cpp)                     | `PipelineClass` : liste ordonnée de steps (`addStep`). `configure(json)` applique un script, `exportConfig()` / `exportScript()` le réécrivent. `run()` les exécute dans l'ordre et s'arrête au premier échec ou à la première exception. `run()` sait aussi reprendre (`resolveStart`), faire une pause (`requestPause`) et réécrire le script après chaque step (`setScriptPath`). |
| [pipeline_builder.h](../include/backend/core/pipeline/pipeline_builder.h) / [.cpp](../src/backend/core/pipeline/pipeline_builder.cpp)     | **Registre des steps** (`stepRegistry()` : nom → classe), `createStep`, `getRegisteredStepNames`, `buildPipeline(json)` (validation + contrôle de l'enchaînement), `buildPipelineFromFile`.                                                                                                                                                                                          |
| [run_state.h](../include/backend/core/pipeline/run_state.h) / [.cpp](../src/backend/core/pipeline/run_state.cpp)                          | `RunState`, champ `"run"` du script : `status`, `completedSteps`, consignes `skipCompletedSteps` / `startFromStep`, `checkpointDirectory`. `RunState::fromJson` le valide.                                                                                                                                                                                                           |
| [checkpoint.h](../include/backend/core/checkpoint/checkpoint.h) / [.cpp](../src/backend/core/checkpoint/checkpoint.cpp)                   | `CheckpointClass` : sauvegarde des sorties de chaque step, un dossier `NN_nom/` par step, `manifest.json` écrit en dernier. Valide si même step, mêmes paramètres et même empreinte des entrées. `registerType` pour un nouveau type.                                                                                                                                                |
| [checkpoint_types.h](../include/backend/core/checkpoint/checkpoint_types.h) / [.cpp](../src/backend/core/checkpoint/checkpoint_types.cpp) | (Dé)sérialisation JSON des types du pipeline : `Tiles` (fiches seulement, sans pixels), `PairMatches`, `Positions`, `Territories`.                                                                                                                                                                                                                                                   |
| [intensity_correction_checkpoint.h](../include/backend/core/checkpoint/intensity_correction_checkpoint.h) / [.cpp](../src/backend/core/checkpoint/intensity_correction_checkpoint.cpp) | Sérialisation de `IntensityCorrection`, ajoutée par `CheckpointClass::registerType` plutôt que dans `checkpoint_types.cpp`. Modèle pour un nouveau type.                                                                                                                                                                                                        |
| [final_verification_checkpoint.h](../include/backend/core/checkpoint/final_verification_checkpoint.h) / [.cpp](../src/backend/core/checkpoint/final_verification_checkpoint.cpp) | Sérialisation de `VerificationReport` pour restaurer le rapport de `final-verification`.                                                                                                                                                                                                                                                    |
| [intensity_correction.h](../include/backend/core/data/intensity_correction.h)                                                             | `DataNs::IntensityCorrection` : `tileGains` (un gain par tuile, dans l'ordre de `tiles`), `fieldStrength`, `isolatedTileIndices`. Produit par `intensity-correction`.                                                                                                                                                                                                                |
| [memory_scope.h](../include/backend/core/memory/memory_scope.h) / [.cpp](../src/backend/core/memory/memory_scope.cpp)                     | `MemoryNs::MemoryScopeClass` : mesure la mémoire résidente d'un bloc (avant, pic échantillonné par un thread, après, pic du processus, en Mio) et l'écrit dans un JSON à la destruction. N'affiche rien, ne lève pas d'exception. Utilisé par aucun step pour l'instant.                                                                                                         |
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
| [SiftStepClass](../src/backend/steps/sift_step.cpp)                          | `sift`              | `"tiles"` ; `overlap`, `maxFeatures`, `loweRatio`, `contrastThreshold`, `claheClipLimit`, `minMatches`                      | `"matches"` : `vector<PairMatches>`       | `SiftNs::SiftMatcherClass` sur les zones communes des voisines droite/bas                                         |
| [LightGlueStepClass](../src/backend/steps/lightglue_step.cpp)                | `lightglue`         | `"tiles"` ; `modelPath`, `overlap`, `minMatches`                                                                             | `"matches"` : `vector<PairMatches>`       | `LightGlueNs::LightGlueMatcherClass` (modèle ONNX)                                                                |
| [AbsolutePosStepClass](../src/backend/steps/absolute_pos_step.cpp)           | `absolute-pos`      | `"tiles"`, `"matches"` ; `referenceTileId` (−1 : plus petit identifiant)                                                    | `"positions"` : `vector<TilePositionData>` | `PositioningNs::solveRelativePositions` (moindres carrés par SVD, translations seules, `rotationRadians` = 0)      |
| [IntensityCorrectionStepClass](../src/backend/steps/intensity_correction_step.cpp) | `intensity-correction` | `"tiles"`, `"matches"` ; `method` (`gain` ou `gain-and-field`), paramètres de l'égalisation                             | `"intensityCorrection"` : `IntensityCorrection` | `BrightnessEqualizationNs::BrightnessEqualizerClass` (réduction 1/8, pixels lus par `PhaseCorrelationStepClass::readPixels`). Ne modifie aucun pixel. |
| [GraphCutStepClass](../src/backend/steps/graph_cut_step.cpp)                 | `graphcut`          | `"tiles"`, `"positions"` : `vector<TilePositionData>`                                                                        | `"territories"` : `vector<TileTerritory>` | `GraphCutNs::ImagePretreatmentClass` + `GraphCutNs::GraphCutSeamFinderClass`                                      |
| [FinalVerificationStepClass](../src/backend/steps/final_verification_step.cpp) | `final-verification` | chemins des images de référence et reconstruite                                                                             | `"verificationReport"` : `VerificationReport` | `VerificationNs::compareImages` : dimensions et métadonnées, sans comparaison des pixels |
| [DynamicProgrammingDijkstraStepClass](../src/backend/steps/dynamic_programming_dijkstra_step.cpp) | `dynamic-programming-dijkstra` | `"tiles"`, `"positions"`, `"intensityCorrection"` ; `method`, `costFunction`, `corridorRadius` | `"seamlines"` : `Seamlines` | `DynamicProgrammingDijkstraNs::SeamFinderClass` sur chaque recouvrement droite/dessous, après division par les gains |

Ces 11 steps sont dans le registre : on peut les mettre dans un script JSON. `absolute-pos` et `graphcut` exigent des fichiers nommés `..._r<ligne>_c<colonne>.tif` : le format par défaut de `import` (`img_C…_R…`) ne convient pas (voir [AbsolutePosStep.md](steps/AbsolutePosStep.md)).

[matching_step_utils.h](../include/backend/steps/matching_step_utils.h) / [.cpp](../src/backend/steps/matching_step_utils.cpp) n'est pas un step : c'est le code commun aux steps d'appariement (`collectPairs`, `readImage`, validation des correspondances, `restoreCoordinates`, mise en forme JSON).

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
| **`positioning/`** [position_solver](../src/backend/algorithms/stitching/positioning/position_solver.cpp)                                                                                                            | `PositioningNs::solveRelativePositions()` : ajuste toutes les contraintes `B − A` à la fois (SVD, poids égaux), référence en (0, 0). Les tuiles non reliées à la référence sont signalées (`connected = false`). Utilisé par `absolute-pos`.                     |
| ↳ [tile_positioner](../src/backend/algorithms/stitching/positioning/tile_positioner.cpp)                                                                                                                             | `PositioningNs::computeTilePositions()` : chemin indépendant du pipeline. À partir des **chemins**, refait matcher (`sift` par défaut) → vote 2D sur chaque paire, puis `solveRelativePositions()`. Écrit les résidus dans un `ostream`.                       |

### `algorithms/rejection/` : rejet des outliers 🆕

| Fichier                                                                                   | Rôle                                                                                                                                                                                                                            |
| ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [outlier_rejector.h](../include/backend/algorithms/rejection/outlier_rejector.h)          | **Interface** `RejectionNs::OutlierRejectorClass` : `estimate(matches, alignment)`.                                                                                                                                             |
| [vote_rejector](../src/backend/algorithms/rejection/vote_2d/vote_rejector.cpp)           | `VoteRejectorClass` (namespace `stitching::rejection`, alias `RejectionNs::VoteRejectorClass`) : vote de Hough 2D + médiane itérative, translation pure. `filterMatches` / `filterPairMatches` retirent les aberrants. Doc : [Vote2D.md](algorithms/rejection/vote_2d/Vote2D.md). |

RANSAC (`algorithms/rejection/ransac/`) a été **supprimé** par #63 : le vote 2D est la seule méthode de rejet. Il n'y a pas encore de step de rejet dans le pipeline.

### `algorithms/brightness_equalization/` : égalisation de luminosité 🆕

Port du notebook `egalisation_gain_champ_final.ipynb`, namespace `BrightnessEqualizationNs`. Même découpage que la corrélation de phase. Le step `intensity-correction` l'appelle pour estimer les gains ; la correction des pixels (`correctTile`) reviendra à l'assemblage.

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
| [solver](../src/backend/algorithms/seamline/dynamic_programming_dijkstra/solver.cpp)                                                                                                         | Section 6.3 : `solveEdge()`, point d'entrée unique des 3 méthodes (`SolverMethod`), avec mesures de temps et de mémoire. Rayon du corridor par défaut : `DEFAULT_CORRIDOR_RADIUS` (18 px). |
| [junctions](../src/backend/algorithms/seamline/dynamic_programming_dijkstra/junctions.cpp)                                                                                                   | Sections 7-8 : points de jonction entre 4 tuiles et réseau global de coutures sur la grille (`H_r_c` / `V_r_c`, `SeamGrid`, `solveGlobalNetwork`).                     |
| [overlay](../src/backend/algorithms/seamline/dynamic_programming_dijkstra/overlay.cpp)                                                                                                       | Dessin du réseau de coutures sur une mosaïque (`drawSeamNetwork`, illustration).                                                                                       |

Le step `dynamic-programming-dijkstra` (Nde-Code, arrivé avec la PR #72) appelle `SeamFinderClass::computeSeam` sur chaque recouvrement, après division par les gains de `intensityCorrection`. Voir [dp-dijkstrat.md](steps/dp-dijkstrat.md).

**`graph_cut/`** (namespace `GraphCutNs`, ancien `Seamline`) : coupe de graphe d'OpenCV, paire par paire.

| Fichier                                                                                         | Rôle                                                                                                                                                                                                                                                                  |
| ----------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [image_pretreatment](../src/backend/algorithms/seamline/graph_cut/image_pretreatment.cpp)       | `ImagePretreatmentClass` : relit les tuiles depuis le disque (`read16Bit`, `readSmallFloat`), calcule les coins et les masques réduits. `parseTileName()` extrait ligne et colonne de `..._rLL_cCC.tif`. **Aussi utilisé par le positionnement.**                     |
| [graph_cut_seam_finder](../src/backend/algorithms/seamline/graph_cut/graph_cut_seam_finder.cpp) | `GraphCutSeamFinderClass` : `computeSeamline()` (coupe à échelle réduite, carte de provenance pleine résolution, mosaïque 8 bits d'aperçu), `extractPolygons()` (territoires par tuile, `DataNs::TileTerritory`), `verifyTiling()` (contrôle du pavage, `SeamCheck`). |

### `frontend/` : interface graphique

| Fichier                                                                                         | Rôle                                                                                                                                                                                      |
| ----------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [PipelineJson.h](../include/frontend/PipelineJson.h) / [.cpp](../src/frontend/PipelineJson.cpp) | Fabrique le JSON du pipeline à partir de champs de formulaire (`makeStep`, `parameterFromText`, `writePipelineJson`), namespace `FrontendNs`. Appelé par aucun code pour l'instant. |

Les fenêtres Qt n'existent que sur des branches, et ne sont pas reliées au calcul : voir §8.

### `tests/`

Tous en GoogleTest et lancés par `ctest`, avec le label `unit` (environ 150 tests au 07/11/2026). Seule exception : `lightglue_model`, label `integration`, qui fait une vraie inférence avec un modèle ONNX téléchargé à la configuration (ou fourni par `-DLIGHTGLUE_TEST_MODEL=<modèle.onnx>`). Les tests n'utilisent pas d'images externes (décision du 02/11/2026).

| Dossier                                                          | Tests                                    | Ce qu'ils vérifient                                                                                                                                                                                                                                                                                                                |
| ---------------------------------------------------------------- | ---------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [core/](../tests/core/)                                          | `PipelineBuilder.*` (5), `Reprise.*` (6), `PyramidalTiffReaderClass.*` (5), `MemoryScope.*` (3) | Registre (11 steps, constante `REGISTERED_STEP_COUNT` à augmenter pour chaque nouveau step), validation du script, fusion des options, aller-retour `exportConfig` → `buildPipeline`, pipeline réel import → phase-correlation sur deux tuiles 16 bits générées, reprise sur checkpoints (échec, paramètres modifiés, `startFromStep`, `skipCompletedSteps`, pause, `run.json`), aller-retour des types sérialisés, lecture par région du TIFF pyramidal, mesure de mémoire. |
| [brightness_equalization/](../tests/brightness_equalization/) 🆕 | `BrightnessEqualization.*` (3)           | 6 tuiles générées avec gains et champ connus, gris et couleur, 8 et 16 bits ; cas limites.                                                                                                                                                                                                                                         |
| [intensity_correction/](../tests/intensity_correction/)          | `IntensityCorrectionStepTest.*` (8)      | Step `intensity-correction` (version de #72) : enregistrement, gain connu, gains centrés sur 1, méthode inconnue, `matches` absent, identifiant inconnu, paramètre invalide, enchaînement avec `dynamic-programming-dijkstra`. |
| [phase_correlation/](../tests/phase_correlation/) 🆕             | `PhaseCorrelation.*` (3), `PhaseCorrelationConfidence.*` (10) | Paires générées à vérité sous-pixel exacte dans les 4 directions, les deux passes, les cas limites ; contrôles de confiance. `etude_confiance` est compilé mais n'est pas un test (outil de l'étude, voir [EtudeConfiance.md](algorithms/stitching/phase_correlation/EtudeConfiance.md)). |
| [seamline/](../tests/seamline/)                                  | 71 tests                                 | `DynamicProgrammingDijkstraNs` : carte de coût, catalogue, DP, Dijkstra, solveur, jonctions, parité avec le notebook.                                                                                                                                                                                                              |
| [steps/](../tests/steps/)                                        | `SiftStepTest.*` (7), `LightGlueStepTest.*` (5), `AbsolutePosStep.*` et `PositionSolver.*` (15) | Steps d'appariement (formats 8/16 bits, repères, succès partiels, JSON ; inférence LightGlue remplacée par une fabrique de test) et placement global (contraintes, graphe déconnecté, référence).                                                                                         |

Pour ajouter des tests : un dossier `tests/<module>/` avec son `CMakeLists.txt` (modèle : [tests/brightness_equalization/CMakeLists.txt](../tests/brightness_equalization/CMakeLists.txt)), et une ligne `add_subdirectory` dans [cmake/tests_runner.cmake](../cmake/tests_runner.cmake) ; l'exécutable de test se lie à `kikirpa_backend`.

### `docs/`

| Fichier                                                                                      | Contenu                                                                                                                                |
| -------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| [PhaseCorrelation.md](algorithms/stitching/phase_correlation/PhaseCorrelation.md) 🆕         | Méthode, paramètres (avec leur nom dans le step), limites et résultats chiffrés comparés au Python. **La meilleure doc du projet.**    |
| [OverlapEstimation.md](algorithms/stitching/overlap_estimation/OverlapEstimation.md) 🆕      | La 1re passe commune.                                                                                                                  |
| [BrightnessEqualization.md](algorithms/brightness_equalization/BrightnessEqualization.md) 🆕 | Modèle, méthodes A et B, paramètres, vérification.                                                                                     |
| [EtudeConfiance.md](algorithms/stitching/phase_correlation/EtudeConfiance.md)                | Indicateur de confiance de la corrélation de phase : étude et seuils retenus.                                                         |
| [Vote2D.md](algorithms/rejection/vote_2d/Vote2D.md) (+ [README](algorithms/rejection/vote_2d/README.md)) | Rejet des aberrants par vote de Hough 2D et médiane itérative.                                                              |
| [pipeline.md](core/pipeline.md)                                                              | Le pipeline : format du script, contrôles, exécution, reprise et checkpoints, les 11 steps enregistrés. |
| [PyramidalTiff.md](core/PyramidalTiff.md)                                                    | Format du cache TIFF pyramidal, écriture et lecture par région.                                                                        |
| [steps/README.md](steps/README.md)                                                           | Index des steps : ce que chacun lit et écrit, lien vers sa fiche. Chaque fiche commence par ses **entrées et sorties**. |
| [AJOUTER_UN_STEP.md](steps/AJOUTER_UN_STEP.md)                                               | Ajouter un step : exemple complet compilé (code, registre, nouveau type et checkpoint, test GoogleTest, fiche), liste de contrôle. |
| [ImportStep.md](steps/ImportStep.md)                                                         | Step `import` : paramètres, format des noms de fichiers à donner pour la suite du pipeline, fiche d'une tuile, checkpoint. |
| [PhaseCorrelationStep.md](steps/PhaseCorrelationStep.md)                                     | Step `phase-correlation` : paramètres, conventions d'axes et de signe, résolution réduite, confiance, échecs. |
| [SiftStep.md](steps/SiftStep.md), [LightGlueStep.md](steps/LightGlueStep.md)                 | Steps `sift` et `lightglue` : paramètres, bandes communes, coordonnées, échecs. |
| [AbsolutePosStep.md](steps/AbsolutePosStep.md)                                               | Step `absolute-pos` : calcul, nommage des fichiers exigé, erreurs. |
| [IntensityCorrectionStep.md](steps/IntensityCorrectionStep.md)                               | Step `intensity-correction` : paramètres, convention des gains, échecs, checkpoint. |
| [GraphCutStep.md](steps/GraphCutStep.md)                                                     | Step `graphcut` : fonctionnement, territoires écrits, mémoire, limite de 255 tuiles. |
| [COMPRENDRE_LE_PROJET.md](COMPRENDRE_LE_PROJET.md)                                           | **À lire en premier** : vue d'ensemble, lancer le pipeline, entrées et sorties, mémoire, algorithmes, frontend, ce qui reste à faire, lexique. |
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
     phase-correlation · sift · lightglue (relisent les .tif sources ; au choix)
                                  ▼
                        "matches" : vector<PairMatches>
                                  │       PairMatches = { tileIdA, tileIdB, direction, featureMatches }
                                  │       Matches     = { pointsInA[], pointsInB[], confidenceScores[],
                                  │                       translationX, translationY }
                                  │
                  (rejet des aberrants : algorithme vote 2D prêt, pas encore de step)
                                  │
              ┌───────────────────┴────────────────────┐
              ▼                                        ▼
   AbsolutePosStepClass                    IntensityCorrectionStepClass (relit les .tif)
              ▼                                        ▼
   "positions" : vector<TilePositionData>  "intensityCorrection" : IntensityCorrection
   { tileId "rLL_cCC", position,           { tileGains[], fieldStrength, isolatedTileIndices[] }
     rotationRadians = 0 }                             │
              │                                        │ (appliqué par personne pour l'instant)
   GraphCutStepClass (relit les .tif sources)          │
              ▼                                        │
   "territories" : vector<TileTerritory>               │
   { tileIndex, polygons, holes } par tuile            │
              └───────────────────┬────────────────────┘
                                  ▼
                       (assemblage → export : PR #69, branche fe/export)
```

**Un type C++ par clé** : le registre vérifie l'enchaînement d'après le _nom_ de type déclaré (`"Tiles"`, `"PairMatches"`…). `DataStoreClass::get<T>` exige, lui, le _type C++_ exact. Un nouveau step doit donc réutiliser ces types, sinon `std::bad_any_cast` à l'exécution. Ce sont aussi ces 6 types que les checkpoints savent sérialiser :

| Nom (JSON)            | Clé                   | Type C++                                             |
| --------------------- | --------------------- | ---------------------------------------------------- |
| `Tiles`               | `tiles`               | `DataNs::TileList` = `vector<shared_ptr<TileClass>>` |
| `PairMatches`         | `matches`             | `vector<DataNs::PairMatches>`                        |
| `Positions`           | `positions`           | `vector<DataNs::TilePositionData>`                   |
| `Territories`         | `territories`         | `vector<DataNs::TileTerritory>`                      |
| `IntensityCorrection` | `intensityCorrection` | `DataNs::IntensityCorrection` (enregistré par `registerType`) |
| `VerificationReport` | `verificationReport` | `DataNs::VerificationReport` (enregistré par `registerType`) |

**Les conventions de repère à connaître :**

- **`Direction`** donne la position de B par rapport à A (`Top`, `Bottom`, `Left`, `Right`). Les anciens alias `LeftRight` et `BottomRight` ont disparu 🆕.
- **`Matches::translationX, translationY`** donnent le coin haut-gauche de B dans le repère de A, soit `pointInA − pointInB`. Toujours en pixels de pleine résolution, même quand `phase-correlation` mesure sur un niveau réduit.
- **`Alignment::transformBToA`** est une matrice 3×3 `CV_64F` qui transforme B vers A.
- **`cv::Point` vaut (x, y) = (colonne, ligne).** Le Python de référence stocke (y, x).
- **L'égalisation** identifie les tuiles par leur **indice** dans un vecteur, pas par `tileId`, et prend la translation d'une paire telle quelle. `IntensityCorrection::tileGains` suit donc l'ordre de `"tiles"`.
- **`absolute-pos`** place la tuile de référence en (0, 0) ; les autres positions peuvent être négatives.

---

## 6. Lire un algorithme : l'exemple de la corrélation de phase

C'est le modèle de découpage que tout nouvel algorithme doit suivre (voir [CONVENTIONS.md](CONVENTIONS.md)). Ordre de lecture conseillé :

1. **[phase_correlation_matcher.h](../include/backend/algorithms/stitching/phase_correlation/phase_correlation_matcher.h)** : ce que fait l'algorithme et ses paramètres, documentés dans l'en-tête.
2. **[phase_correlation_matcher.cpp](../src/backend/algorithms/stitching/phase_correlation/phase_correlation_matcher.cpp)** : l'orchestrateur (`match()`), court. Il dit dans quel ordre les modules sont appelés.
3. **[phase_correlation_types.h](../include/backend/algorithms/stitching/phase_correlation/phase_correlation_types.h)** : les structures échangées entre modules.
4. Les trois modules dans l'ordre d'exécution : **PreparationClass → AlgorithmClass → OutputClass**. Chaque header liste les fonctions publiques, avec un commentaire qui dit _quoi_ ; les fonctions internes sont dans un `namespace { }` anonyme du `.cpp`, qui dit _pourquoi_.
5. **[PhaseCorrelation.md](algorithms/stitching/phase_correlation/PhaseCorrelation.md)** pour la théorie et les résultats.
6. **[test_phase_correlation.cpp](../tests/phase_correlation/test_phase_correlation.cpp)** pour voir l'algorithme utilisé.

L'**égalisation** suit exactement le même schéma (BrightnessEqualizerClass → PreparationClass → AlgorithmClass → OutputClass). SIFT, LightGlue, le vote 2D, `DynamicProgrammingDijkstraNs` et GraphCut ne le suivent pas encore.

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
| `computeTilePositions` prend les matches du step d'appariement                       | Non : il refait tout lui-même à partir des chemins de fichiers. Dans le pipeline, c'est `absolute-pos` qui part des matches.                                                                                                                                                                                                      |
| RANSAC existe encore                                                                 | Non : supprimé par #63. Le rejet se fait par vote 2D (`vote_rejector.cpp`), et aucun step ne l'appelle encore : les matches arrivent bruts dans `absolute-pos`.                                                                                                                                                                    |
| `absolute-pos` calcule les rotations                                                 | Non : `PairMatches` ne porte pas d'angle, `rotationRadians` vaut toujours 0.                                                                                                                                                                                                                                                       |
| `intensity-correction` corrige les tuiles                                            | Non : il publie seulement des gains (`IntensityCorrection`). Les pixels sources ne sont jamais modifiés ; la correction doit être appliquée à l'assemblage.                                                                                                                                                                         |
| `ImagePretreatmentClass` ne sert qu'à GraphCut                                       | Le positionnement l'utilise aussi (`read16Bit`, `parseTileName`).                                                                                                                                                                                                                                                                  |
| Les tuiles sont en RGB                                                               | OpenCV donne du **BGR**. `DynamicProgrammingDijkstraNs` attend du **RGB**.                                                                                                                                                                                                                                                         |
| Les champs de `PhaseCorrelationNs::Parameters` portent le nom des paramètres du step | Non 🆕 : les champs sont en anglais (`smoothingSigma`, `railMargin`…), le JSON garde `lissage`, `marge`… (correspondance dans [PhaseCorrelation.md §5](algorithms/stitching/phase_correlation/PhaseCorrelation.md#5-paramètres)).                                                                                                  |
| Le TIFF pyramidal est la sortie du pipeline                                          | C'est un cache d'import, par tuile. Il n'y a pas encore d'export.                                                                                                                                                                                                                                                                  |
| Il existe un exemple de la chaîne complète (import → couture → mosaïque)             | Plus sur `main` : `test_chaine_paire` a été supprimé avec les tests d'intégration (#43). Il reste dans l'historique : `git show c383a38:tests/algorithms/integration/test_chaine_paire.cpp`.                                                                                                                                       |
| Qt est facultatif                                                                    | Plus depuis #43 : sans Qt 6 ni GoogleTest, `cmake` échoue. Sous Windows, Qt s'installe avec aqt et GoogleTest avec vcpkg ([BUILD.md](BUILD.md)).                                                                                                                                                                                   |
| Tout le code suit déjà les conventions                                               | Presque 🆕 : `core/`, `steps/`, `frontend/` et `algorithms/` ont été migrés. Restent les noms internes des tests (français, ou `snake_case` dans `tests/seamline/`), quelques sorties console et les noms des paramètres JSON gardés pour compatibilité : voir [CONVENTIONS.md §9](CONVENTIONS.md#9-ce-qui-nest-pas-encore-migré). |
| `docs/` couvre tous les algorithmes                                                  | Seulement la corrélation de phase (et son indicateur de confiance), le recouvrement, l'égalisation et le vote 2D, plus le pipeline, le TIFF pyramidal, cinq steps et le build. Rien sur les seamlines, SIFT, LightGlue (algorithmes) ni la mesure de mémoire.                                                                    |
| L'interface Qt affiche le calcul                                                     | Sur `main`, il n'y a pas d'interface. Sur les branches, c'est une maquette : la progression est un minuteur de démonstration, sans appel au pipeline (voir §8).                                                                                                                                                                     |

---

## 8. Ce qui n'existe qu'en branche

État au 07/11/2026. Pour savoir où chercher quand un fichier n'est pas sur `main`. Une branche écrite avant le 03/11 doit suivre [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md) §10, §12 si elle touche aux algorithmes, et §13 pour ce qui a changé depuis.

**Backend**

| Je cherche…                                        | Branche (PR)                                   | État                                                                                                                                                                                         |
| -------------------------------------------------- | ---------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Le step de recouvrement                            | `recouvrement_step` (#90)                      | Ouverte, CI rouge. `RecouvrementStep.{h,cpp}` : noms hors [CONVENTIONS.md](CONVENTIONS.md) (`recouvrement_step.h` attendu). Sortie `"overlap-estimates"` : `OverlapEstimates`, type sans sérialisation de checkpoint. |
| Le step d'assemblage (fusion)                      | `feature/fusion-step` (#69, brouillon)         | En conflit avec `main`. `FusionStep.{h,cpp}`, noms hors conventions ; types propres (`FusionInput`, `TilePlacement`).                                                                       |
| Le step d'export                                   | `fe/export` (pas de PR)                        | `include/Backend/steps/export_step.h`, `src/Backend/steps/export_step.cpp` : majuscule dans `Backend`, donc pas compilé sous Linux et macOS. TIFF seulement ; types propres (`CompositeBlock`). |
| Corrections GraphCut                               | `GraphCutStep_Fixes` (#86)                     | En conflit, CI rouge.                                                                                                                                                                         |
| Nouveau parseur de pipeline                        | `pipeline-parser-2.0` (#79)                    | CI rouge. Modifie surtout `tests/core/test_pipeline.cpp`, ajoute un `pipeline.json` à la racine.                                                                                             |
| Test 2×2 de bout en bout                           | `feature/full-2x2-pipeline` (pas de PR)        | Obsolète : écrit sur l'ancien code (namespace `egalisation`), steps `position`/`equalization`/`assembly`/`export` qui n'existent pas sur `main`, test non déclaré dans CMake.            |
| Le step RANSAC                                     | `RansacStep` (pas de PR)                       | Obsolète : RANSAC a été supprimé par #63, et les fichiers sont dans l'ancienne arborescence (`include/Backend/steps/`).                                                                     |

Les trois branches de fin de chaîne (`feature/fusion-step`, `fe/export`, `feature/full-2x2-pipeline`) définissent chacune leurs propres types de mosaïque, incompatibles entre eux.

**Frontend.** Aucune branche n'est reliée au calcul : la progression est un minuteur de démonstration (`SimulationRunner`), l'export de la mosaïque affiche « backend non connecté ».

| Branche (PR)                    | Dernier commit | État                                                                                                                                                                                       |
| ------------------------------- | -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `fr-rahim` (#91)                | 05/11          | **Seule intégrée au build de `main`** (`cmake/frontend.cmake`, dossiers en minuscules, partie de `main` du 04/11). Ajoute l'assistant « nouveau projet » (`ProjectSetupModelClass`) : grille, validation, script `import` + `phase-correlation` validé par `buildPipeline`, 7 tests. CI verte, mergeable. |
| `Valerie`                       | 06/11          | La plus avancée côté interface : code rangé en `app/`, `canvas/`, `workspace/`, `project/`, `home/`, `common/`. Remplace le `CMakeLists.txt` racine (`project(Frontend)`), dossiers `Backend`/`Frontend` en majuscules : ne peut pas être mergée telle quelle. |
| `frontendJonas`                 | 05/11          | Fusion de `Valerie` (au 04/11) et de `frontendMathias`, plus `main` du 03/11. N'a pas la refonte de `Valerie` du 06/11.                                                                    |
| `frontendMathias`               | 04/11          | `frontend` + étape courante, journal et aperçu de mosaïque (image factice).                                                                                                               |
| `frontend` (#9, fermée)         | 03/11          | Tronc commun des trois précédentes, parti de `main` du 20/10.                                                                                                                             |

⚠️ `datasetdetest/` (4 TIFF, environ 167 Mo, commit `5615d48`) a été retiré de `Valerie`, `frontendJonas`, `frontendMathias` et `frontend` le 07/11, et ajouté à leur `.gitignore`. Les fichiers restent dans l'historique de ces branches : les merger dans `main` uniquement en « Squash and merge ».

Pour voir un fichier d'une branche sans changer de branche :

```bash
git show origin/fe/dp-dijkstrat:include/backend/steps/dynamic_programming_dijkstra_step.h
```
