# Migration : réorganisation du code (nov. 2026)

Ce document décrit tout ce qui a changé dans la branche `reformat-v3` par rapport à l'ancien `main`, et comment adapter une branche existante une fois que `reformat-v3` sera fusionnée dans `main`. Les règles à suivre pour le nouveau code sont dans `docs/CONVENTIONS.md`.

## Résumé

- `core/` est découpé en sous-dossiers par rôle, avec des fichiers en `snake_case` et un namespace par dossier.
- Les dossiers `Backend` et `Frontend` passent en minuscules ; les steps passent en `snake_case`.
- Les classes, variables et champs sont renommés en anglais, sans abréviation. Les classes de `core` reçoivent le suffixe `Class`.
- Le format JSON des scripts de pipeline et des checkpoints change (voir §4) : les anciens fichiers ne sont plus lisibles.
- La logique n'a pas changé, à trois exceptions (voir §8).
- Compilation complète et les 87 tests (`ctest`) : OK.

## 1. Dossiers et fichiers

| Avant | Après |
|---|---|
| `include/Backend`, `src/Backend` | `include/backend`, `src/backend` |
| `include/Frontend`, `src/Frontend` | `include/frontend`, `src/frontend` |
| `core/Config.h` | `core/config/pipeline_config.h` |
| `core/Types.h` | `core/data/algorithm_types.h` |
| `core/Tuile.h` / `.cpp` | `core/data/tile.h` / `.cpp` |
| `core/Step.h` / `.cpp` | `core/step/step.h` / `.cpp` |
| `core/DataStore.h` | `core/step/data_store.h` |
| `core/Pipeline.h` / `.cpp` | `core/pipeline/pipeline.h` / `.cpp` |
| `core/PipelineBuilder.h` / `.cpp` | `core/pipeline/pipeline_builder.h` / `.cpp` |
| `core/RunState.h` / `.cpp` | `core/pipeline/run_state.h` / `.cpp` |
| `core/Checkpoint.h` / `.cpp` | `core/checkpoint/checkpoint.h` / `.cpp` |
| `core/CheckpointTypes.h` / `.cpp` | `core/checkpoint/checkpoint_types.h` / `.cpp` |
| `core/PyramidalTiffWriter.h` / `.cpp` | `core/io/pyramidal_tiff_writer.h` / `.cpp` |
| `steps/GraphCutStep.*` | `steps/graph_cut_step.*` |
| `steps/ImportStep.*` | `steps/import_step.*` |
| `steps/PhaseCorrelationStep.*` | `steps/phase_correlation_step.*` |

Supprimés : `src/run_pipeline.cpp` (retiré aussi par `main`), `include/frontend/stub.cpp`, `src/frontend/ToDelete.cpp`.

Nouveau : `algorithms/legacy_type_aliases.h` (temporaire, voir §9).

## 2. Namespaces

Le namespace `stitching` est remplacé par un namespace par dossier : `DataNs`, `ConfigNs`, `StepNs` (contrat d'un step), `PipelineNs`, `CheckpointNs`, `IONs`, `StepsNs` (steps concrets), `FrontendNs`. Les algorithmes restent pour l'instant dans `stitching::…`.

## 3. Classes et types renommés

| Avant | Après |
|---|---|
| `Step`, `DataStore`, `Pipeline`, `Checkpoint`, `Tuile` | `StepClass`, `DataStoreClass`, `PipelineClass`, `CheckpointClass`, `TileClass` |
| `ImportStep`, `GraphCutStep`, `PhaseCorrelationStep` | `ImportStepClass`, `GraphCutStepClass`, `PhaseCorrelationStepClass` (namespace `StepsNs`) |
| `Tiles` | `TileList` |
| `Niveau` | `Level` (`largeur`/`hauteur`/`blocsX`/`blocsY`/`gris` deviennent `width`/`height`/`blocksCountX`/`blocksCountY`/`greyscale`) |
| `RecouvrementReel` | `OverlapEstimate` (`translation`/`overlap`/`rapport` deviennent `translationBInA`/`overlapFraction`/`peakRatio`) |
| `Seamline::TileTerritory` | `DataNs::TileTerritory` (alias `Seamline::TileTerritory` conservé ; `tile_index` devient `tileIndex`) |
| `TAILLE_BLOC`, `NB_NIVEAUX` | `BLOCK_SIZE`, `LEVEL_COUNT` |

## 4. Format JSON (changement incompatible)

Configuration d'un step :

| Avant | Après |
|---|---|
| `Option { type, key, value }` | `StepParameter { valueType, name, value }` |
| `PipelineOption { type, key }` | `DataSlot { dataType, key }` |
| `Signature { pipelineOptions }` | supprimée |
| `StepConfig { name, options, pipelineOptions, signature }` | `StepConfig { name, parameters, inputs, outputs }` |

Exemple d'un paramètre : `{"type":"int","key":"nbPoints","value":10}` devient `{"valueType":"int","name":"nbPoints","value":10}`.

Champ `"run"` du script : `skip` devient `skipCompletedSteps`, `from` devient `startFromStep`, `checkpointDir` devient `checkpointDirectory`.

Checkpoints : le `manifest.json` utilise `parameters` (au lieu de `options`) et `dataType` (au lieu de `type`). Les fichiers de données changent de clés (`PairMatches` : `tileIdA`, `tileIdB`, `direction`, `pointsInA`, `pointsInB`, `confidenceScores`, `translationX`, `translationY` ; `Positions` : `tileId`, `rotationRadians` ; `Territories` : `tileIndex`). Les anciens checkpoints sont jugés périmés et les steps sont refaits.

Les noms des paramètres du step `phase-correlation` (`niveau`, `lissage`, `seuilRapport`, `marge`…) et ceux de `import` (`path`, `format`) n'ont pas changé.

## 5. API de `core`

| Avant | Après |
|---|---|
| `Step::config`, `getConfig()` | `configJson`, `getConfigJson()` |
| `Step::outputData`, `exportData()` | `result`, `getResult()` |
| `Step::data` | `dataStore` |
| `Step::option("x")` | `getParameter("x")` |
| `inputFingerprint()` | `externalInputsFingerprint()` |
| `DataStore::set(key, v)` | `add(key, v)` (erreur si la clé existe) ou `replace(key, v)` |
| `Pipeline::add(step)` | `PipelineClass::addStep(step)` |
| `Pipeline::getData()` | `getDataStore()` |
| `parseRunState(json)` | `RunState::fromJson(json)` |
| `registeredSteps()` | `getRegisteredStepNames()` |
| `Tuile::setVoisins`, `getNiveau`, `blocDe` | `TileClass::setNeighbours`, `getLevel`, `getBlockAt` |

Champs des types de `algorithm_types.h` :

| Type | Avant | Après |
|---|---|---|
| `Matches` | `pointsA`, `pointsB`, `scores`, `dx`, `dy` | `pointsInA`, `pointsInB`, `confidenceScores`, `translationX`, `translationY` |
| `PairMatches` | `idA`, `idB`, `dir`, `matches` | `tileIdA`, `tileIdB`, `direction`, `featureMatches` |
| `PairContext` | `dir`, `overlap`, `translation` | `direction`, `overlapFraction`, `estimatedTranslation` |
| `Alignment` | `transform`, `inliers` | `transformBToA`, `inlierMask` |
| `TilePositionData` | `tile_id`, `rotation` | `tileId`, `rotationRadians` |
| `TileImage` | `path`, `col`, `row`, `image` | `filePath`, `gridColumn`, `gridRow`, `pixels` |
| `Direction` | `LeftRight`, `BottomRight` (alias) | `Right`, `Bottom` |

## 6. Conventions appliquées

- Anglais partout dans le code ; commentaires en français.
- `camelCase` pour variables et fonctions ; plus d'abréviations ni de variables d'une lettre.
- Includes depuis `include/backend`, jamais de `"../"`, groupés et triés.
- Fonctions internes à un `.cpp` dans un `namespace { }` anonyme.
- Commentaires de `core/` et de `steps/` entièrement réécrits.

Détail dans `docs/CONVENTIONS.md`.

## 7. Dépendances

Aucune nouvelle dépendance. Le CMake de `main` est conservé ; seuls les chemins `src/backend` et `include/backend` ont été mis à jour.

## 8. Changements de comportement

Hors renommages :
1. `PipelineClass` n'a plus de destructeur par défaut explicite (il bloquait son déplacement et empêchait `buildPipeline` de la renvoyer).
2. `DataStoreClass::set` est remplacé par `add` (refuse un doublon) et `replace` (écrase). Les steps et les chargements de checkpoint utilisent `replace`, comme l'ancien `set`.
3. Un caractère parasite (`*`) dans `step.cpp` a été supprimé.

## 9. Ce qui reste à faire

- Les algorithmes (`algorithms/`) utilisent encore `namespace stitching`, des noms français (`Parametres`, `raisonEchec`, `Algorithme`, `Preparation`, `Sortie`, `lissage`…) et `legacy_type_aliases.h`, qui est un fichier temporaire à supprimer.
- Les tests contiennent encore des noms français (`verifier`, `LARGEUR_TUILE`…).
- Les pages des algorithmes (`docs/algorithms/`) citent encore plusieurs anciens noms. `docs/core/Pipeline.md`, `docs/steps/PhaseCorrelationStep.md` et `docs/CARTE_ARCHITECTURE.md` sont à jour.
- À vérifier : `TileClass` calcule les dimensions des niveaux par décalage (`>>`, arrondi à l'inférieur), alors que le TIFF est écrit avec `cv::resize` ; pour une dimension impaire, les deux peuvent différer d'un pixel.

## 10. Adapter une branche existante

Une fois que `reformat-v3` sera fusionnée dans `main`, vous pourrez adapter votre branche ainsi :

1. Ramener `main` dans votre branche : `git fetch && git merge origin/main`.
2. Remplacer les anciens includes : `core/Types.h` devient `core/data/algorithm_types.h`, etc. (tableau du §1).
3. Qualifier les types avec leur namespace (`DataNs::Matches`, `StepNs::StepClass`…) ou, en attendant, inclure `algorithms/legacy_type_aliases.h` dans un fichier d'algorithme.
4. Renommer les champs, méthodes et clés JSON (§3 à §5).
5. Compiler (`cmake --build --preset windows-msvc`) et lancer `ctest --preset windows-msvc`.

## 11. Vérification effectuée

Compilation complète depuis zéro (50 fichiers, 0 erreur, 0 avertissement) et 87 tests sur 87 réussis (MSVC 2022, OpenCV 4.10, libtiff 4.7, Qt 6.8).
