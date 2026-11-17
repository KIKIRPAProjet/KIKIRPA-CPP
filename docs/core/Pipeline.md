# Pipeline : script JSON, registre des steps, exécution

Code : `core/pipeline/` (`PipelineClass`, `buildPipeline`, `RunState`), `core/step/` (`StepClass`, `DataStoreClass`), `core/config/pipeline_config.h` (format du script).
Test : `tests/core/test_pipeline.cpp` (`ctest -L unit`).

## Lancer un pipeline

L'exécutable `stitching` accepte un script JSON en ligne de commande :

```sh
stitching --pipeline pipeline.json
stitching --pipeline pipeline.json --resume
stitching --help
```

Les chemins relatifs du script sont résolus depuis le dossier du script. `--resume`
lit l'état sauvegardé dans `run.checkpointDirectory/run.json` ; sans cette option,
toutes les étapes sont relancées. Codes de sortie : `0` succès, `1` étape en
échec, `2` argument/script invalide, `3` erreur de fichier. Le script
[pipeline.json](../../pipeline.json) est un exemple complet ; il attend des
images compatibles dans un dossier `images/` à côté du script.

L'API C++ reste disponible pour les tests et les intégrations :

```cpp
PipelineNs::PipelineClass pipeline = PipelineNs::buildPipelineFromFile("pipeline.json");   // ou buildPipeline(json)
bool ok = pipeline.run([](const std::string& message) { std::cout << message << '\n'; });
const auto& tiles = pipeline.getDataStore().get<DataNs::TileList>("tiles");   // sorties des steps
```

## Format du script

```json
{
  "pipeline": {
    "steps": [
      { "name": "import",
        "parameters": [ { "valueType": "string", "name": "path", "value": "images/" } ] },
      { "name": "phase-correlation" }
    ]
  }
}
```

- **`name`** : le nom d'un step du registre (voir plus bas).
- **`parameters`** : un paramètre absent garde sa **valeur par défaut**. Un paramètre fourni doit exister dans le step, avec le même `valueType`.

| `valueType` | Valeurs acceptées |
|---|---|
| `string` | chaîne |
| `int` | entier (2.5 refusé) |
| `float` | nombre (2 accepté) |
| `bool` | `true` / `false` |

- **`inputs`** et **`outputs`** (`{ "dataType": "Tiles", "key": "tiles" }`) peuvent figurer dans le JSON, par exemple quand il est écrit par `PipelineClass::exportConfig` ou par la GUI. Ils sont **ignorés** à la lecture : c'est le step qui les fixe.

⚠️ Ce format remplace celui d'avant la réorganisation (`options`, `type`, `key`, `pipelineOptions`, `signature`) : un ancien script est refusé.

## Contrôles faits par `buildPipeline`

`buildPipeline` lève `std::invalid_argument`, avec le chemin de l'erreur (`pipeline.steps[1] ('graphcut'): …`), si :

- le JSON n'a pas la forme `{ "pipeline": { "steps": [...] } }` ;
- un step est inconnu ;
- un paramètre est inconnu, en double, d'un autre type ou avec une valeur du mauvais type ;
- **une entrée d'un step n'est produite par aucun step précédent**, ou l'est avec un autre type. Par exemple, `graphcut` juste après `import` est refusé : personne ne produit `positions`.

## Exécution

`PipelineClass::run` part d'un `DataStoreClass` vide et exécute les steps dans l'ordre. Il s'arrête au premier step qui renvoie `false` **ou lève une exception**, et renvoie `false`. Le message (`getResult()` du step, ou le texte de l'exception) va au logger, ou sur `std::cerr` s'il n'y en a pas.

## Reprise, pause, checkpoints

Code : `core/pipeline/run_state.h`, `core/checkpoint/`. Test : `tests/core/test_reprise.cpp`.

```cpp
PipelineNs::RunState runState = pipeline.getRunState();
runState.checkpointDirectory = "output/checkpoints";   // active les checkpoints et la reprise
runState.startFromStep = "phase-correlation";          // facultatif : refaire à partir de ce step
pipeline.setRunState(runState);
pipeline.setScriptPath("output/checkpoints/run.json");  // script réécrit après chaque step, relu pour reprendre
pipeline.run(log);
```

Le champ facultatif **`"run"`** du script, à côté de `"pipeline"`, contient deux sortes de valeurs.

**L'état, écrit par le pipeline :**
- `status` : `idle`, `running`, `paused`, `failed` ou `completed` ;
- `completedSteps` : les noms des steps déjà faits, dans l'ordre.

**Les consignes, écrites par l'appelant (la GUI), et remises à leur défaut au démarrage :**
- `skipCompletedSteps` (`true` par défaut) : saute les steps déjà faits dont le checkpoint est valide ; `false` : repart de zéro ;
- `startFromStep` : le nom du step où (re)démarrer. Il ne peut pas être après le premier step à refaire.

**`checkpointDirectory`** : dossier des checkpoints. **Vide par défaut, ce qui veut dire ni checkpoint ni reprise.**

**Dans le dossier des checkpoints :**
- Il y a un sous-dossier par step (`00_import/`, `01_phase-correlation/`…), avec les sorties du step et un `manifest.json` écrit en dernier.
- Un checkpoint est valide si le manifest est complet, pour le même step, avec les mêmes paramètres.
- Dès qu'un step est invalide, lui et tous les suivants sont refaits.
- `setScriptPath()` réécrit le script complet (`pipeline` + `run`) après chaque step, par exemple dans `<checkpoints>/run.json`.

**Sérialisation des sorties** (`core/checkpoint/checkpoint_types.cpp`) : un fichier `<clé>.json` par sortie.

| Type | Ce qui est écrit |
|---|---|
| `Tiles` | les fiches `TileClass` (`id`, `source`, `tiff`, `width`, `height`, `type`, voisines). **Les pixels ne sont pas recopiés** : à la reprise, le TIFF pyramidal doit toujours exister. |
| `PairMatches` | `tileIdA`, `tileIdB`, `direction`, `pointsInA`, `pointsInB`, `confidenceScores`, `translationX`, `translationY` |
| `Positions` | `tileId`, `position`, `rotationRadians` |
| `Territories` | `tileIndex`, `polygons`, `holes` |
| `Seamlines` | une entrée par couture : `tileIdA`, `tileIdB`, `direction`, `points` |
| `IntensityCorrection` | `tileGains`, `fieldStrength`, `isolatedTileIndices` |

- `CompositeBlocks` n'est pas encore sérialisé : son checkpoint est incomplet
  et `fusion` est donc réexécuté lors d'une reprise.
- Un nouveau type s'ajoute dans `pipelineTypes()`, ou par `CheckpointClass::registerType`.
- Un step dont une sortie n'a pas de sérialisation est refait à chaque reprise. Le journal le signale : `checkpoint incomplete`.
- Les checkpoints écrits avant la réorganisation sont jugés périmés : les steps sont refaits.

**Empreinte des entrées.**
- `StepClass::externalInputsFingerprint()` décrit ce que le step lit hors du pipeline. Elle est gardée dans le manifest.
- `ImportStepClass` y met le nom, la taille et la date de chaque image source. Une image ajoutée, retirée ou modifiée périme donc le checkpoint de l'import, et toute la suite est refaite.

**Pause** : `PipelineClass::requestPause()` est thread-safe. La pause est prise en compte entre deux steps, et le statut passe à `paused`. Un nouveau `run()` reprend au step suivant.

## Steps enregistrés

| `name` | Classe | Entrées | Sorties |
|---|---|---|---|
| `import` | `StepsNs::ImportStepClass` | — | `tiles` : `Tiles` |
| `phase-correlation` | `StepsNs::PhaseCorrelationStepClass` | `tiles` : `Tiles` | `matches` : `PairMatches` |
| `sift` | `StepsNs::SiftStepClass` | `tiles` : `Tiles` | `matches` : `PairMatches` |
| `lightglue` | `StepsNs::LightGlueStepClass` | `tiles` : `Tiles` | `matches` : `PairMatches` |
| `absolute-pos` | `StepsNs::AbsolutePosStepClass` | `tiles` : `Tiles`, `matches` : `PairMatches` | `positions` : `Positions` |
| `graphcut` | `StepsNs::GraphCutStepClass` | `tiles` : `Tiles`, `positions` : `Positions` | `territories` : `Territories` |
| `intensity-correction` | `StepsNs::IntensityCorrectionStepClass` | `tiles` : `Tiles`, `matches` : `PairMatches` | `intensityCorrection` : `IntensityCorrection` |
| `dynamic-programming-dijkstra` | `StepsNs::DynamicProgrammingDijkstraStepClass` | `tiles` : `Tiles`, `positions` : `Positions`, `intensityCorrection` : `IntensityCorrection` | `seamlines` : `Seamlines` |
| `fusion` | `StepsNs::FusionStepClass` | `tiles` : `Tiles`, `positions` : `Positions`, `territories` : `Territories` | `compositeBlocks` : `CompositeBlocks` |
| `export` | `StepsNs::ExportStepClass` | `compositeBlocks` : `CompositeBlocks` | — (écrit `mosaic.tif`) |

`phase-correlation`, `sift` et `lightglue` sont trois façons de produire `matches` : un pipeline en utilise une seule. `graphcut` et `dynamic-programming-dijkstra` sont deux méthodes de couture. Une fiche par step dans [docs/steps/](../steps/).
`fusion` est le producteur de blocs composites ; `export` est terminal et écrit la mosaïque TIFF.

## Contrats des données du `DataStoreClass`

Le nom de clé et le type C++ concret font partie du contrat : le `dataType` du
`DataSlot` sert à valider l'enchaînement dans le JSON, mais ne vérifie pas le
type effacé par `std::any`. Tous les producteurs et consommateurs d'une clé
doivent donc employer le même type C++.

| Clé | `dataType` | Type C++ concret | Producteur / consommateur actuel |
|---|---|---|---|
| `tiles` | `Tiles` | `DataNs::TileList` | Produit par `import` ; consommé par tous les steps suivants |
| `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` | Produit par `phase-correlation`, `sift`, `lightglue` ; consommé par `absolute-pos`, `intensity-correction` |
| `positions` | `Positions` | `std::vector<DataNs::TilePositionData>` | Produit par `absolute-pos` ; consommé par `graphcut`, `dynamic-programming-dijkstra` |
| `intensityCorrection` | `IntensityCorrection` | `DataNs::IntensityCorrection` | Produit par `intensity-correction` ; consommé par `dynamic-programming-dijkstra` et lu facultativement par `fusion` (gain par tuile uniquement) |
| `territories` | `Territories` | `std::vector<DataNs::TileTerritory>` | Produit par `graphcut` ; consommé par `fusion` |
| `compositeBlocks` | `CompositeBlocks` | `DataNs::CompositeBlockList` | Produit par `fusion` ; consommé par `export` |

Les contrats actifs ci-dessus décrivent les échanges de la branche, mais
certains choix de représentation et de cycle de vie restent à valider :

| Donnée demandée | État du code | Décisions à faire valider |
|---|---|---|
| Transformations et rejets | `DataNs::Alignment` contient une matrice `cv::Mat` et un masque d'inliers ; aucun step ni type sérialisable ne l'échange | Type de transformation, repère, unités, lien avec les identifiants de paire et forme sérialisée |
| Corrections lumineuses | `intensityCorrection` échange `DataNs::IntensityCorrection`; `fusion` applique le gain par tuile, mais pas `fieldStrength` | Valider la formule et l'application complète du modèle, ainsi que la sérialisation |
| Blocs de mosaïque | `compositeBlocks` échange `DataNs::CompositeBlockList` en mémoire et n'est pas sérialisé dans les checkpoints | Valider le cycle de vie et le stockage temporaire pour les grandes mosaïques |

### Proposition à faire valider

La proposition ci-dessous s'appuie sur les types et algorithmes déjà présents ;
elle **n'est pas une décision approuvée** et ne doit pas être implémentée comme
un contrat définitif sans validation.

Avant de faire évoluer ou de dépendre davantage de ces données, les responsables
doivent confirmer les décisions ci-dessous. Les propositions restent des
hypothèses et ne remplacent pas les contrats actuellement utilisés par le code.

| Décision à valider | État actuel / proposition | Conséquence si le choix change |
|---|---|---|
| Identité canonique d'une tuile | `TileClass` et `PairMatches` utilisent un entier ; `TilePositionData` un nom de grille et `TileTerritory` un indice de vecteur. Proposition : utiliser l'identifiant entier stable attribué à l'import partout. | Modification des types partagés, des steps de positionnement et de découpe, et des checkpoints concernés. |
| Repère et unités communs | Les coordonnées sont des pixels, X vers la droite et Y vers le bas ; l'origine au centre du pixel supérieur gauche est documentée pour les correspondances. Proposition : conserver les pixels de pleine résolution pour toutes les sorties de pipeline et expliciter si chaque position désigne un centre ou un coin. | Conversion entre niveaux pyramidaux, algorithmes, géométrie des territoires et export. |
| Modèle des transformations | `Alignment` contient aujourd'hui un `cv::Mat` 3×3 `CV_64F` et un masque d'inliers, sans identifiants de paire. À valider : modèle autorisé (translation, affine ou projectif), sens B→A, représentation sérialisée et relation entre masque et correspondances. | Compatibilité RANSAC, positionnement global et format des checkpoints. |
| Modèle et application des corrections lumineuses | `intensityCorrection` expose les gains par tuile et `fieldStrength`; `fusion` applique actuellement l'inverse de `tileGains` seulement. À valider : formule retenue et application du champ spatial. | Résultat photométrique de la mosaïque et sérialisation des sorties. |
| Géométrie des territoires | `TileTerritory` stocke des contours entiers et un `tileIndex`. À valider : référence de tuile canonique, repère, règle d'arrondi et convention des trous/contours. | Alignement des masques de découpe sur les pixels exportés et stabilité si l'ordre des tuiles change. |
| Représentation et durée de vie des blocs mosaïque | Le contrat courant est `DataNs::CompositeBlockList` en mémoire (`imageRgb`, `provenance`, `mask`, `blockRect`). À valider : quand basculer vers des fichiers temporaires et qui en possède le nettoyage. | Budget mémoire, reprise sur checkpoint et contrat de l'exporteur. |
| Paramètres et erreurs | Le JSON accepte `string`, `int`, `float`, `bool`; les erreurs de configuration sont des `invalid_argument`, les échecs d'exécution des résultats d'étape transmis au logger. À valider : règles de nommage/rétrocompatibilité et quels échecs de paire sont tolérés plutôt que fatals. | Compatibilité des scripts et interprétation fiable des runs partiels. |

| Échange proposé | Clé / `dataType` | Type partagé proposé | Convention proposée |
|---|---|---|---|
| Correspondances par paire | `matches` / `PairMatches` | `std::vector<DataNs::PairMatches>` (existant) | Identifiants `tileIdA`/`tileIdB` égaux à `TileClass::getId()` ; points et translations en pixels de pleine résolution |
| Transformations et rejets | `alignments` / `PairAlignments` | nouveau `std::vector<DataNs::PairAlignment>` contenant les deux identifiants, `Direction`, une transformation B→A fixe 3×3 en `double` et le masque d'inliers aligné sur `featureMatches` | Même ordre que `matches` ou, de préférence, correspondance explicite par identifiants ; sérialisation numérique explicite plutôt que `cv::Mat` opaque |
| Positions | `positions` / `Positions` | `std::vector<DataNs::TilePositionData>` (existant) | À terme, identifiant entier `TileClass::getId()` ; origine mosaïque en haut à gauche, coordonnées X droite / Y bas, pixels de pleine résolution, rotation en radians. Ce changement remplacerait le `tileId` texte historique |
| Corrections lumineuses | `intensityCorrection` / `IntensityCorrection` (actif) | `DataNs::IntensityCorrection` (existant) | Le type contient `tileGains`, `fieldStrength` et `isolatedTileIndices`; valider l'identité par tuile et la formule complète à appliquer. `fusion` n'utilise actuellement que l'inverse de `tileGains`. |
| Territoires | `territories` / `Territories` | `std::vector<DataNs::TileTerritory>` (existant) | Coordonnées entières de pleine résolution dans le repère mosaïque ; remplacer `tileIndex` par l'identifiant canonique si les consommateurs doivent survivre à un changement d'ordre |
| Blocs de mosaïque | `compositeBlocks` / `CompositeBlocks` (actif) | `DataNs::CompositeBlockList` de `DataNs::CompositeBlock` (actif) | Blocs de 512×512 pixels en mémoire ; `blockRect` est exprimé dans le repère mosaïque et le step `export` les consomme. Un stockage temporaire reste une évolution à valider. |
| Tuiles | `tiles` / `Tiles` | `DataNs::TileList` (existant) | Les pixels restent dans les TIFF ; les fiches portent l'identité et la grille. L'identifiant doit être la référence unique des autres échanges |

Cette proposition harmonise les identifiants actuellement hétérogènes
(`PairMatches` utilise des entiers, `TilePositionData` un nom de grille et
`TileTerritory` un indice de vecteur). Elle implique une migration des
consommateurs et des checkpoints `Positions`/`Territories`, ainsi que
l'enregistrement de sérialiseurs pour les nouveaux types. À l'exception de
`compositeBlocks` / `CompositeBlocks`, déjà utilisés par `fusion` et `export`,
les clés et types proposés ne sont pas encore des contrats actifs du registre.

### Repères, paramètres et erreurs

- Les points de `Matches` sont exprimés en pixels, origine au centre du pixel
  supérieur gauche, axe X vers la droite et axe Y vers le bas. La translation
  décrit le coin supérieur gauche de B dans le repère de A ; elle vaut
  `pointsInA - pointsInB`.
- `TilePositionData::position` désigne le coin supérieur gauche d'une tuile
  dans le repère absolu de la mosaïque. `TileTerritory` est également dans ce
  repère. La convention de rotation et son application aux territoires restent
  à valider avant d'ajouter un step qui les consomme.
- Attention à l'identité des tuiles : `import` associe les images par groupes
  colonne/ligne configurables (par défaut `img_C…_R…`), tandis que
  `TilePositioner` et `ImagePretreatment` attendent un suffixe `_r…_c…`.
  Le nom `rLL_cCC` de `TilePositionData::tileId` doit être relié sans ambiguïté
  aux identifiants entiers de `TileClass` avant d'enchaîner ces algorithmes dans
  un pipeline ; leur ordre dans deux vecteurs distincts ne suffit pas comme
  contrat.
- Les paramètres du script ont un nom `camelCase` et un `valueType` parmi
  `string`, `int`, `float` et `bool`. Les noms historiques en français du step
  `phase-correlation` sont conservés pour ne pas casser les scripts existants.
- La lecture commune est `buildPipelineFromFile()` ; `buildPipeline()` vérifie
  la structure, les steps, les paramètres et l'enchaînement des entrées avant
  l'exécution. Une erreur de configuration lève `std::invalid_argument` avec
  le chemin du champ. Un échec d'exécution renvoie `false` avec sa cause dans
  `result["error"]`, ensuite transmise au logger du pipeline.

Le pipeline enregistré prend en charge la chaîne `import` → appariement →
positionnement → découpe → `fusion` → `export`. Le test
`Pipeline2x2CliTest.RunsAndResumesFullPipelineFromScriptDirectory` lance cette
chaîne avec l'exécutable CLI sur un jeu 2×2 synthétique et vérifie la mosaïque
exportée ainsi que la reprise depuis les checkpoints.

## Reprise des branches existantes

La carte d'architecture (`CARTE_ARCHITECTURE.md`, §8) décrit les branches et PR
antérieures à la réorganisation. Leurs sources ne sont pas présentes dans cette
copie ; ces références permettent de cibler les reprises, pas de prétendre
qu'elles ont déjà été fusionnées ou testées ici.

| Éléments référencés | Traitement compatible avec cette branche |
|---|---|
| `sift-step` (#32), `lightglue-step` (#27) | Reprendre les algorithmes utiles, mais adapter chaque step à `StepsNs`, `StepClass`, `StepConfig`, aux clés/types documentés ci-dessus et au CMake actuel. Ne pas importer leurs anciennes déclarations de tests ou leur code commun sans vérifier les dépendances. |
| `confiance-correlation-phase` (#48) | Examiner séparément l'étude et valider le sens du score avant de modifier `PairMatches::confidenceScores` ou le contrat de `matches`. |
| `GraphCut_Fixed` (#49) | Le déplacement de `TileTerritory` est déjà présent dans `DataNs::TileTerritory`; ne pas reprendre une seconde définition. |
| `RansacStep` | La carte signale un ancien `.cpp` hors de `src/backend/`. Reprendre le comportement utile dans un step conforme, avec une sortie de transformation typée et sérialisable à valider. |
| `fe/pipeline-json-parser` (#47/#41) | Les squelettes fusion/export utilisent une architecture et des types différents (pixels en mémoire). Ne pas les fusionner tels quels ; ne reprendre que des éléments après comparaison avec les contrats et le traitement par blocs de cette branche. |
| `amo/IntensityCorrectionStep` | La carte la décrit comme vide ; l'algorithme d'égalisation présent dans cette copie est la base réutilisable, mais son type de sortie `DataStoreClass` reste à définir. |

Une reprise effective nécessite les commits ou PR sources et une comparaison
avec `main`. Dans cette copie, limiter chaque changement à un sujet
indépendamment compilable et testable ; ne pas annoncer l'intégration complète
avant qu'un script unique soit exécuté de l'import à l'export sur les tuiles
2×2.

## Ajouter un step

Marche à suivre complète, avec un exemple compilé et testé (en-tête, constructeur, `execute()`, registre, nouveau type et checkpoint, test GoogleTest, fiche) : [steps/AJOUTER_UN_STEP.md](../steps/AJOUTER_UN_STEP.md).

En bref : une classe qui hérite de `StepNs::StepClass`, dont le constructeur par défaut fixe `name`, `parameters`, `inputs` et `outputs` ; une ligne dans `stepRegistry()` ; `REGISTERED_STEP_COUNT` + 1 dans `tests/core/test_pipeline.cpp`. **Le type C++ derrière une clé est le même pour tous les steps**, sinon `get<T>` lève `std::bad_any_cast` à l'exécution.
