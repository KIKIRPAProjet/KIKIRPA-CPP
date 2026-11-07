# Migration : réorganisation du code (nov. 2026)

Ce document décrit tout ce qui a changé dans la branche `reformat-v3` par rapport à l'ancien `main`, et comment adapter une branche existante une fois que `reformat-v3` sera fusionnée dans `main`. La migration des algorithmes, faite ensuite, est décrite au [§12](#12-migration-des-algorithmes-branche-refactoralgorithms-conventions), et les changements apportés depuis par les PR mergées du 04 au 06/11 au [§13](#13-depuis-la-migration-04-au-06112026). Les règles à suivre pour le nouveau code sont dans `docs/CONVENTIONS.md`.

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

Nouveau : `algorithms/legacy_type_aliases.h` (temporaire, voir §9 ; supprimé depuis par la migration des algorithmes, §12).

### Build (PR #67)

Les fichiers de `cmake/` passent en `snake_case` et les bibliothèques prennent le préfixe `kikirpa_` (l'exécutable reste `stitching`, nom provisoire) :

| Avant | Après |
|---|---|
| `cmake/Dependencies.cmake` | `cmake/dependencies_manager.cmake` |
| `cmake/Backend.cmake` | `cmake/backend.cmake` |
| `cmake/Frontend.cmake` | `cmake/frontend.cmake` |
| `cmake/Tests.cmake` | `cmake/tests_runner.cmake` |
| `cmake/Install.cmake` | `cmake/install.cmake` |
| `cmake/ClangTidy.cmake` | `cmake/clang_tidy.cmake` |
| cible `stitching_backend` | `kikirpa_backend` |
| cible `stitching_frontend` | `kikirpa_frontend` |

## 2. Namespaces

Le namespace `stitching` est remplacé par un namespace par dossier : `DataNs`, `ConfigNs`, `StepNs` (contrat d'un step), `PipelineNs`, `CheckpointNs`, `IONs`, `StepsNs` (steps concrets), `FrontendNs`. Les algorithmes sont restés dans `stitching::…` jusqu'à leur migration (§12), qui leur a donné aussi un namespace par dossier.

## 3. Classes et types renommés

| Avant | Après |
|---|---|
| `Step`, `DataStore`, `Pipeline`, `Checkpoint`, `Tuile` | `StepClass`, `DataStoreClass`, `PipelineClass`, `CheckpointClass`, `TileClass` |
| `ImportStep`, `GraphCutStep`, `PhaseCorrelationStep` | `ImportStepClass`, `GraphCutStepClass`, `PhaseCorrelationStepClass` (namespace `StepsNs`) |
| `Tiles` | `TileList` |
| `Niveau` | `Level` (`largeur`/`hauteur`/`blocsX`/`blocsY`/`gris` deviennent `width`/`height`/`blocksCountX`/`blocksCountY`/`greyscale`) |
| `RecouvrementReel` | `OverlapEstimate` (`translation`/`overlap`/`rapport` deviennent `translationBInA`/`overlapFraction`/`peakRatio`) |
| `Seamline::TileTerritory` | `DataNs::TileTerritory` (`tile_index` devient `tileIndex` ; l'alias `Seamline::TileTerritory` a été supprimé au §12) |
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

- ~~Les algorithmes (`algorithms/`) utilisent encore `namespace stitching`, des noms français et `legacy_type_aliases.h`~~ : fait, voir §12.
- Les tests contiennent encore des noms internes français (`verifier`, `LARGEUR_TUILE`…) ; voir `docs/CONVENTIONS.md` §9.
- ~~Les pages des algorithmes (`docs/algorithms/`) citent encore plusieurs anciens noms~~ : mises à jour au §12.
- À vérifier : `TileClass` calcule les dimensions des niveaux par décalage (`>>`, arrondi à l'inférieur), alors que le TIFF est écrit avec `cv::resize` ; pour une dimension impaire, les deux peuvent différer d'un pixel.

## 10. Adapter une branche existante

Une fois que `reformat-v3` sera fusionnée dans `main`, vous pourrez adapter votre branche ainsi :

1. Ramener `main` dans votre branche : `git fetch && git merge origin/main`.
2. Remplacer les anciens includes : `core/Types.h` devient `core/data/algorithm_types.h`, etc. (tableau du §1).
3. Qualifier les types avec leur namespace (`DataNs::Matches`, `StepNs::StepClass`…). `algorithms/legacy_type_aliases.h` n'existe plus (§12).
4. Renommer les champs, méthodes et clés JSON (§3 à §5).
5. Dans les `CMakeLists.txt` de vos tests, lier `kikirpa_backend` au lieu de `stitching_backend`, et déclarer un nouveau dossier de tests dans `cmake/tests_runner.cmake` (et non plus `cmake/Tests.cmake` ni `cmake/algorithms.cmake`). Voir §1, « Build ».
6. Si la branche touche aux algorithmes, appliquer aussi le §12.
7. Compiler (`cmake --build --preset windows-aqt`, ou `linux`, `macos`) et lancer `ctest --preset windows-aqt`. Le preset `windows-msvc` a été remplacé par `windows-aqt` (#59) : voir [BUILD.md](BUILD.md).

## 11. Vérification effectuée

Compilation complète depuis zéro (50 fichiers, 0 erreur, 0 avertissement) et 87 tests sur 87 réussis (MSVC 2022, OpenCV 4.10, libtiff 4.7, Qt 6.8).

## 12. Migration des algorithmes (branche `refactor/algorithms-conventions`)

Tous les algorithmes de `algorithms/` suivent maintenant `docs/CONVENTIONS.md` : fichiers en `snake_case`, noms anglais sans abréviation, un namespace par dossier, suffixe `Class` pour les classes, types partagés qualifiés (`DataNs::Matches`), fonctions et constantes internes dans un `namespace { }` anonyme du `.cpp`, commentaire de chemin en première ligne, includes groupés et triés. **`algorithms/legacy_type_aliases.h` est supprimé.**

Ce qui ne change pas :
- la logique et les résultats numériques (les 87 tests passent à l'identique) ;
- les noms des steps (`phase-correlation`, `graphcut`) et de leurs paramètres JSON (`niveau`, `lissage`, `marge`, `scale`…) ;
- les clés JSON lues par SIFT, LightGlue, RANSAC et le vote, et les valeurs texte des options (voir [CONVENTIONS.md §9](CONVENTIONS.md#9-ce-qui-nest-pas-encore-migré)) ;
- les commentaires restent en français ; les messages des exceptions et des échecs passent en anglais (les tests qui cherchaient un mot du message ont été adaptés).

### Dossiers, fichiers et namespaces

| Avant | Après |
|---|---|
| `algorithms/stitching/FeatureMatcher.h` (`stitching::matching`) | `algorithms/stitching/feature_matcher.h` (`StitchingNs`) |
| `algorithms/stitching/correlation_phase/` (`stitching::matching`, `::correlation_phase`) | `algorithms/stitching/phase_correlation/` (`PhaseCorrelationNs`) |
| ↳ `CorrelationPhase.h`, `Types.h`, `Preparation.h`, `Algorithme.h`, `Sortie.h` | `phase_correlation_matcher.h`, `phase_correlation_types.h`, `preparation.h`, `algorithm.h`, `output.h` |
| `algorithms/stitching/recouvrement/EstimationRecouvrement.h` | `algorithms/stitching/overlap_estimation/overlap_estimator.h` (`OverlapEstimationNs`) |
| `algorithms/stitching/sift/SiftMatcher.h`, `SiftMatcherAlgorithm.h` | `algorithms/stitching/sift/sift_matcher.h`, `sift_matcher_algorithm.h` (`SiftNs`) |
| `algorithms/stitching/lightglue/LightGlueMatcher.h` | `algorithms/stitching/lightglue/lightglue_matcher.h` (`LightGlueNs`) |
| `algorithms/stitching/positioning/TilePositioner.h` (`stitching::positioning`) | `algorithms/stitching/positioning/tile_positioner.h` (`PositioningNs`) |
| `algorithms/rejection/OutlierRejector.h` (`stitching::rejection`) | `algorithms/rejection/outlier_rejector.h` (`RejectionNs`) |
| `algorithms/rejection/ransac/Ransac{Algorithm,Preprocessor,Rejector,Types}.h` (`stitching::rejection::ransac`) | `algorithms/rejection/ransac/ransac_{algorithm,preprocessor,rejector,types}.h` (`RansacNs`) |
| `algorithms/egalisation/` (`stitching::egalisation`) | `algorithms/brightness_equalization/` (`BrightnessEqualizationNs`) |
| ↳ `Egalisation.h`, `Types.h`, `Preparation.h`, `Algorithme.h`, `Sortie.h` | `brightness_equalizer.h`, `brightness_equalization_types.h`, `preparation.h`, `algorithm.h`, `output.h` |
| `algorithms/seamline/dp_dijkstra/*.hpp` (`seamlib`) | `algorithms/seamline/dynamic_programming_dijkstra/*.h` (`DynamicProgrammingDijkstraNs`), mêmes noms de fichiers |
| `algorithms/seamline/dp_dijkstra/export.hpp` (macro `KIRK_API`) | supprimé (bibliothèque statique) |
| `algorithms/seamline/graphcut/` (`Seamline`) | `algorithms/seamline/graph_cut/` (`GraphCutNs`) |
| ↳ `GraphCutSeamFinder.h`, `ImagePretreatment.h` | `graph_cut_seam_finder.h`, `image_pretreatment.h` |
| `algorithms/legacy_type_aliases.h` | supprimé : écrire `DataNs::Matches`, `DataNs::OverlapEstimate`… |
| `tests/correlation_phase/test_correlation_phase.cpp` (cible `correlation_phase_tests`) | `tests/phase_correlation/test_phase_correlation.cpp` (`phase_correlation_tests`) |
| `tests/egalisation/test_egalisation.cpp` (cible `egalisation_tests`) | `tests/brightness_equalization/test_brightness_equalization.cpp` (`brightness_equalization_tests`) |
| `tests/seamline/test_maps.hpp` | `tests/seamline/test_maps.h` |
| `docs/algorithms/stitching/correlation_phase/CorrelationPhase.md` | `docs/algorithms/stitching/phase_correlation/PhaseCorrelation.md` |
| `docs/algorithms/stitching/recouvrement/EstimationRecouvrement.md` | `docs/algorithms/stitching/overlap_estimation/OverlapEstimation.md` |
| `docs/algorithms/egalisation/Egalisation.md` | `docs/algorithms/brightness_equalization/BrightnessEqualization.md` |

Les tests, dont le namespace était `stitching::tests`, sont dans un namespace anonyme. Les suites `CorrelationPhase.*` et `Egalisation.*` s'appellent `PhaseCorrelation.*` et `BrightnessEqualization.*`.

### Interfaces

| Avant | Après |
|---|---|
| `matching::FeatureMatcher::match(a, b, ctx, out)` | `StitchingNs::FeatureMatcherClass::match(imageA, imageB, context, matches)` |
| `rejection::OutlierRejector::estimate(matches, out)` | `RejectionNs::OutlierRejectorClass::estimate(matches, alignment)` |

### Corrélation de phase

| Avant | Après |
|---|---|
| `matching::CorrelationPhase` | `PhaseCorrelationNs::PhaseCorrelationMatcherClass` |
| `CorrelationPhase::Parametres`, `correlation_phase::Parametres` | `PhaseCorrelationNs::Parameters` |
| `raisonEchec()` | `getFailureReason()` |
| `Preparation`, `Algorithme`, `Sortie` | `PreparationClass`, `AlgorithmClass`, `OutputClass` |
| `PaireOrientee { A, B, extA, extB, position, inversee }` | `OrientedPair { imageA, imageB, outsideMaskA, outsideMaskB, direction, isSwapped }` |
| `Bandes { a, b, origine, marge }` | `Strips { stripA, stripB, origin, searchMargin }` |
| `Correlation { dy, dx, pic, surface, autorise }` | `Correlation { shiftY, shiftX, peakHeight, surface, allowedMask }` |
| `Diagnostic { rapportMarge, picLibre, rapportLibre, horsMarge }` | `Diagnostic { peakRatioInMargin, unconstrainedPeak, unconstrainedPeakRatio, isOutsideMargin }` |
| `Mesure { tx, ty, rapport }` | `Measurement { translationX, translationY, peakRatio }` |
| `Recouvrement { a, b, origineA, decalage, marge }` | `Overlap { regionA, regionB, originInA, roundedTranslation, borderMargin }` |
| `Correspondance { pointA, translation, confiance }` | `Correspondence { pointInA, localTranslation, confidence }` |
| `Preparation::verifierParametres`, `orienter`, `orienterTranslation`, `bandes`, `zoneCommune`, `recouvrement`, `valides`, `remplirVides` | `PreparationClass::validateParameters`, `orientPair`, `orientTranslation`, `extractNominalStrips`, `extractCommonArea`, `extractOverlap`, `computeValidMask`, `fillEmptyPixels` |
| `Algorithme::mesurerGlobal`, `affinerGlobal`, `diagnostiquerGlobal`, `raffiner` | `AlgorithmClass::measureGlobalTranslation`, `refineGlobalTranslation`, `diagnoseGlobalTranslation`, `measureLocalPoints` |
| `Sortie::construire`, `echecPaire`, `echecBandes`, `echecGlobal`, `echecZoneCommune`, `echecAffinage` | `OutputClass::buildMatches`, `describePairFailure`, `describeStripsFailure`, `describeGlobalFailure`, `describeCommonAreaFailure`, `describeRefinementFailure` |
| fonctions `private` (`correlationDePhase`, `spectreCroise`, `rapportPics`, `zoneExterieure`, `enGris`, `nombre`…) et constantes (`RAYON_PIC`, `MARGE_DEUXIEME_PASSE`…) | dans un namespace anonyme du `.cpp` : `correlatePhase`, `computeCrossSpectrum`, `computePeakRatio`, `computeOutsideMask`, `convertToGrey`, `formatNumber`… ; `PEAK_RADIUS`, `SECOND_PASS_MARGIN`… |

Champs de `Parameters` (le nom du paramètre du step `phase-correlation` ne change pas) :

| Avant (= paramètre du step) | Après |
|---|---|
| `lissage` | `smoothingSigma` |
| `seuilRapport` | `minimumPeakRatio` |
| `lissageLocal` | `localSmoothingSigma` |
| `ecartMax` | `maximumLocalDeviation` |
| `espacementMin` | `minimumPointSpacing` |
| `marge` | `railMargin` |
| `fractionValideMin` | `minimumValidFraction` |
| `nbPoints` | `maximumPointCount` |
| `fenetre` | `patchSize` |
| `facteurDft` | `upsamplingFactor` |

Step `phase-correlation` : `PhaseCorrelationInput::parametres` → `parameters`, `niveau` → `level` (le paramètre JSON reste `niveau`).

### Estimation du recouvrement

| Avant | Après |
|---|---|
| `matching::EstimationRecouvrement` | `OverlapEstimationNs::OverlapEstimatorClass` |
| `estimer(a, b, ctx)` → `RecouvrementReel` | `estimate(imageA, imageB, context)` → `DataNs::OverlapEstimate` |
| `raisonEchec()` | `getFailureReason()` |
| `zonesCommunes(a, b, translation)` | `computeCommonAreas(sizeA, sizeB, translation)` |

### SIFT et LightGlue

| Avant | Après |
|---|---|
| `matching::SiftMatcher` | `SiftNs::SiftMatcherClass` |
| `matching::runSiftMatching` | `SiftNs::runSiftMatching` |
| `matching::LightGlueMatcher` | `LightGlueNs::LightGlueMatcherClass` |
| `LightGlueMatcher::preprocessImage` (méthode privée) | `preprocessImage` (namespace anonyme) |
| `kMinConfidenceScore` | `MINIMUM_CONFIDENCE_SCORE` |

### Rejet des outliers

RANSAC a été supprimé ensuite par #63, et le vote a changé de dossier : voir [§13](#13-depuis-la-migration-04-au-06112026).

| Avant | Après |
|---|---|
| `rejection::RansacRejector`, `rejection::VoteRejector` | `RansacNs::RansacRejectorClass`, `RansacNs::VoteRejectorClass` |
| `ransac::RansacAlgorithm`, `ransac::RansacPreprocessor` | `RansacNs::RansacAlgorithmClass`, `RansacNs::RansacPreprocessorClass` |
| `RansacParameters { minMatches, reprojectionThreshold, maxIters, confidence, refineIters, minInliers }` | `RansacParameters { minimumMatchCount, reprojectionThreshold, maximumIterationCount, confidence, refinementIterationCount, minimumInlierCount }` |
| `RansacOutput { transform, inliers, inlierCount }` | `RansacOutput { transformBToA, inlierMask, inlierCount }` |
| `makeParameters`, `RansacPreprocessor::isFinite` | `readParameters`, `isFinitePoint` (namespace anonyme) |

### Positionnement absolu

| Avant | Après |
|---|---|
| `positioning::computeTilePositions(tile_paths, settings, positions, log)` | `PositioningNs::computeTilePositions(tilePaths, settings, positions, log)` |
| `PositioningSettings { overlap, matcher, rejector, matcherParams, rejectorParams }` | `PositioningSettings { overlapFraction, matcherName, rejectorName, matcherParameters, rejectorParameters }` |

### Égalisation de luminosité

| Avant | Après |
|---|---|
| `egalisation::Egalisation` | `BrightnessEqualizationNs::BrightnessEqualizerClass` |
| `reduire`, `estimer`, `raisonEchec`, `corriger`, `pairesDepuisPositions` | `reduceTile`, `estimate`, `getFailureReason`, `correctTile`, `buildPairsFromPositions` |
| `Methode::GainGlobal`, `Methode::GainEtChamp` | `CorrectionMethod::GlobalGain`, `CorrectionMethod::GainAndField` |
| `Parametres { reduction, flou, marge, seuilBas, seuilHaut, iterations, huber, poidsJauge }` | `Parameters { reductionFactor, blurSigma, borderMargin, lowThreshold, highThreshold, iterationCount, huberThreshold, gaugeWeight }` |
| `TuileReduite { lum, pleine }` | `ReducedTile { luminance, fullSize }` |
| `PaireVoisine { a, b, translation }` | `NeighbourPair { tileIndexA, tileIndexB, translation }` |
| `Echantillons { a, b, r2a, r2b, d }` | `PairSamples { tileIndexA, tileIndexB, squaredRadiiA, squaredRadiiB, logDifferences }` |
| `Solution { logGains, c }` | `Solution { logGains, fieldStrength }` |
| `Bilan { ecartMoyen, variationBF, rmsResidu, residuMedian }` | `QualityReport { meanDifference, lowFrequencyVariation, residualRms, medianResidual }` |
| `Resultat { gainsA, gainsB, c, tuilesIsolees, pairesVides, nbEchantillons, avant, apresA, apresB, gain(), champ() }` | `EqualizationResult { gainsA, gainsB, fieldStrength, isolatedTiles, emptyPairs, sampleCount, before, afterA, afterB, getGain(), getFieldStrength() }` |
| `Preparation::verifierParametres`, `reduire`, `depuisReduite`, `verifierEntrees`, `luminance`, `valeurMax` | `PreparationClass::validateParameters`, `reduceTile`, `prepareReducedTile`, `validateInputs`, `computeLuminance`, `maximumValueOfDepth` |
| `Algorithme::echantillons`, `resoudre`, `residus`, `mesurer`, `corrigerReduite` | `AlgorithmClass::collectSamples`, `solveGains`, `computeResiduals`, `measureOverlaps`, `correctReducedLuminance` |
| `Sortie::corriger`, `rapport`, `echecSansEchantillon` | `OutputClass::correctTile`, `buildReport`, `describeNoSampleFailure` |

### Seamline DP / Dijkstra (ancien `seamlib`)

| Avant | Après |
|---|---|
| `seamlib::SeamFinder::compute(left, right)` | `DynamicProgrammingDijkstraNs::SeamFinderClass::computeSeam(overlapLeft, overlapRight)` |
| `SeamFinder::cost_function`, `params`, `method`, `corridor_radius` | `costFunction`, `parameters`, `method`, `corridorRadius` |
| `Method { DP, Dijkstra8, DPDijkstraLocal }` | `SolverMethod { DynamicProgramming, Dijkstra8, DynamicProgrammingDijkstraLocal }` |
| `CostFunction::LocalNorm` | `CostFunction::LocalNormalization` |
| `CostMapParams { w_color, normalization, color_norm, treatment, recall, lam }` | `CostMapParameters { colorWeight, normalization, colorNorm, treatment, recall, recallWeight }` |
| `CostMaps { e_color, e_gradient, treated }` | `CostMaps { colorTerm, gradientTerm, treated }` |
| `Gradients { Gx, Gy, magnitude }` | `Gradients { gradientX, gradientY, magnitude }` |
| `PathResult { path, objective, expanded, pushes, max_heap, settled }` | `PathResult { path, objective, expandedCount, pushCount, maximumHeapSize, settledMask }` |
| `SolveResult { …, wall_s, expanded, pushes, max_heap, cells, corridor_ratio, seed_s, refine_s, mem_est_mb }` | `SolveResult { …, wallSeconds, expandedCount, pushCount, maximumHeapSize, cellCount, corridorRatio, seedSeconds, refineSeconds, estimatedMemoryMegabytes }` |
| `EdgeCost { cost, overlap_px, scale }` | `EdgeCost { cost, overlapPixels, scale }` |
| `SeamGrid { n_rows, n_cols, edges, n_edges(), n_junctions() }` | `SeamGrid { rowCount, columnCount, edges, getEdgeCount(), getJunctionCount() }` |
| `Junction { u, v, score, score_map, time_s }` | `Junction { normalizedRow, normalizedColumn, score, scoreMap, timeSeconds }` |
| `JunctionSet { method, points, total_time_s }` | `JunctionSet { method, points, totalTimeSeconds }` |
| `Endpoints::start_ptr()`, `goal_ptr()` | `Endpoints::getStartPointer()`, `getGoalPointer()` |
| `compute_cost`, `cost_function_name`, `cost_function_from_name`, `all_cost_functions` | `computeCost`, `getCostFunctionName`, `parseCostFunctionName`, `getAllCostFunctions` |
| `cost_color_gradient`, `cost_color_only`, `cost_gradient_only`, `cost_grain_robust`, `cost_local_norm`, `cost_highlight_penalty`, `cost_center_prior` | `computeColorGradientCost`, `computeColorOnlyCost`, `computeGradientOnlyCost`, `computeGrainRobustCost`, `computeLocalNormalizationCost`, `computeHighlightPenaltyCost`, `computeCenterPriorCost` |
| `gradient`, `gradient_magnitude`, `minmax`, `mad_calculator`, `percentile`, `gray_raw`, `compute_cost_maps` | `computeGradients`, `computeGradientMagnitude`, `normalizeMinMax`, `computeMedianAbsoluteDeviation`, `normalizeByPercentile`, `convertToRawGrey`, `computeCostMaps` |
| `edge_weight`, `dp_constrained`, `dp_distance_top` | `computeEdgeWeight`, `findDynamicProgrammingPath`, `computeDynamicProgrammingDistanceFromTop` |
| `dijkstra_path`, `dijkstra_distance_map`, `corridor_mask_from_path`, `hybrid_corridor`, `dp_dijkstra_local` | `findDijkstraPath`, `computeDijkstraDistanceMap`, `buildCorridorMask`, `buildHybridCorridor`, `findDynamicProgrammingDijkstraLocalPath` |
| `method_name`, `method_from_name`, `all_methods`, `path_objective`, `estimate_solver_memory_mb`, `solve_edge` | `getMethodName`, `parseMethodName`, `getAllMethods`, `computePathObjective`, `estimateSolverMemoryMegabytes`, `solveEdge` |
| `edge_key`, `directional_dist`, `choose_junction_point`, `choose_all_junctions`, `edge_endpoints`, `solve_global_network`, `draw_seam_network` | `buildEdgeKey`, `computeDirectionalDistance`, `chooseJunctionPoint`, `chooseAllJunctions`, `computeEdgeEndpoints`, `solveGlobalNetwork`, `drawSeamNetwork` |

Les valeurs texte (`"noise"`, `"percentile"`, `"manhattan"`, `"euclidian"`, `"sigmoid"`, `"caped"`, `"color_gradient"`, `"local_norm"`, `"DP"`, `"Dijkstra-8"`, `"DP+Dijkstra-local"`, `"top"`, `"bottom"`, clés `"H_r_c"` / `"V_r_c"`) ne changent pas.

### Seamline GraphCut (ancien `Seamline`)

| Avant | Après |
|---|---|
| `Seamline::GraphCut` | `GraphCutNs::GraphCutSeamFinderClass` |
| `Seamline::ImagePretreatment` | `GraphCutNs::ImagePretreatmentClass` |
| `ImagePretreatment::read16` | `ImagePretreatmentClass::read16Bit` |
| `Seamline::TileTerritory`, `Seamline::TilePositionData` (alias) | supprimés : `DataNs::TileTerritory`, `DataNs::TilePositionData` |
| `GraphCut::fallbackPixels` | `GraphCutSeamFinderClass::fallbackPixelCount` |
| `SeamCheck { exactTiling, differentPixels, overlapPixels, gapPixels, fallbackPixels, polygons, holes, vertices }` | `SeamCheck { isExactTiling, differentPixelCount, overlapPixelCount, gapPixelCount, fallbackPixelCount, polygonCount, holeCount, vertexCount }` |
| paramètres `tile_paths`, `tile_sizes`, `original_corners`, `corners_small`, `masks_umat_small`, `final_mosaic`, `index_matrix`, `num_tiles` | `tilePaths`, `tileSizes`, `originalCorners`, `smallCorners`, `smallMasks`, `finalMosaic`, `tileIndexMap`, `tileCount` |

### Adapter une branche qui touche aux algorithmes

1. `git fetch && git merge origin/main`.
2. Remplacer les includes et les noms d'après les tableaux ci-dessus. Plus d'`#include "algorithms/legacy_type_aliases.h"` : qualifier les types partagés (`DataNs::Matches`, `DataNs::PairContext`…) et déclarer `using Json = nlohmann::json;` dans son propre namespace si besoin.
3. Dans les `CMakeLists.txt` de tests, `tests/correlation_phase` et `tests/egalisation` deviennent `tests/phase_correlation` et `tests/brightness_equalization`.
4. Compiler et lancer `ctest`. Une branche qui touche LightGlue doit être compilée avec ONNX Runtime (depuis #27, CMake le télécharge et la CI le compile : voir §13).

## 13. Depuis la migration (04 au 06/11/2026)

Les PR mergées après #76 n'ont pas fait de réorganisation d'ensemble, mais certaines suppriment ou déplacent du code. Une branche partie de `main` avant le 04/11 doit en tenir compte.

### Rejet des outliers (#63)

| Avant | Après |
|---|---|
| `algorithms/rejection/ransac/ransac_{algorithm,preprocessor,rejector,types}.h` (`RansacNs`) | **supprimés** : RANSAC n'existe plus |
| `RansacNs::VoteRejectorClass` (dans `ransac_rejector.h`) | `stitching::rejection::VoteRejectorClass` dans `algorithms/rejection/vote_2d/vote_rejector.h`, avec les alias `RejectionNs::VoteRejectorClass` et `RejectionNs::VoteRejector` |
| — | `VoteRejectorClass::filterMatches`, `filterPairMatches`, `toEqualizationJson` |
| `PositioningSettings::rejectorName = "ransac"` | valeurs acceptées : `"vote"` (par défaut) ou `"vote_2d"` |

### Positionnement (#77)

| Avant | Après |
|---|---|
| `solvePositions` (namespace anonyme de `tile_positioner.cpp`) | `PositioningNs::solveRelativePositions(tileCount, offsets, …)` dans `algorithms/stitching/positioning/position_solver.h`, partagé par `computeTilePositions` et le step `absolute-pos` |

### Steps et types ajoutés (#27, #32, #77, #80)

- Registre : `sift`, `lightglue`, `absolute-pos` et `intensity-correction` s'ajoutent à `import`, `phase-correlation` et `graphcut`. Un nouveau step doit aussi augmenter `REGISTERED_STEP_COUNT` dans `tests/core/test_pipeline.cpp` (7 au 07/11).
- Code commun aux steps d'appariement : `steps/matching_step_utils.h` (`collectPairs`, `readImage`, `restoreCoordinates`…).
- `DataNs::IntensityCorrection` est dans **`core/data/intensity_correction.h`**, pas dans `algorithm_types.h`. Sa sérialisation est enregistrée par `CheckpointClass::registerType` (`core/checkpoint/intensity_correction_checkpoint.cpp`), pas dans `checkpoint_types.cpp`. Une branche partie de la PR #72 (`fe/intensity-correction-step`, `fe/dp-dijkstrat`) le déclare encore dans `algorithm_types.h` : retirer cette déclaration et inclure le nouveau header.

### Seamline DP / Dijkstra (#66, #68)

| Avant | Après |
|---|---|
| `corridorRadius = 18` écrit en dur (`solveEdge`, `SeamFinderClass`) | constante `DEFAULT_CORRIDOR_RADIUS` (`solver.h`), même valeur |
| `enum class SolverMethod` | `enum class SolverMethod : std::uint8_t` |

Le comportement de `solveEdge` ne change pas : le corps est seulement découpé en fonctions internes.

### Mémoire (#93)

Nouveau dossier `core/memory/` (`MemoryNs::MemoryScopeClass`). Il n'écrit plus rien sur la console : les mesures vont seulement dans le JSON passé au constructeur.

### Build (#27, #89)

- ONNX Runtime est maintenant **téléchargé à la configuration** s'il n'est pas fourni (`-DONNXRUNTIME_ROOT`) : LightGlue est toujours compilé, y compris par la CI. Le modèle du test `lightglue_model` est aussi téléchargé (`-DLIGHTGLUE_TEST_MODEL` pour en fournir un).
- Sous Windows, tout nouvel exécutable de test lié à `kikirpa_backend` appelle `kikirpa_copy_onnxruntime_dlls(<cible>)` dans son `CMakeLists.txt`.
- La CI compile aussi sous Windows (MSVC, `build-test-windows.yml`).
