# Pipeline : script JSON, registre des steps, exécution

Code : `core/pipeline/` (`PipelineClass`, `buildPipeline`, `RunState`), `core/step/` (`StepClass`, `DataStoreClass`), `core/config/pipeline_config.h` (format du script).
Test : `tests/core/test_pipeline.cpp` (`ctest -L unit`).

## Lancer un pipeline

Il n'y a pas d'exécutable en ligne de commande, et la GUI n'appelle pas encore le backend : aujourd'hui, le pipeline se lance seulement depuis le code, par exemple dans un test. Exemple complet et formats des sorties : [COMPRENDRE_LE_PROJET.md](../COMPRENDRE_LE_PROJET.md).

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

`phase-correlation`, `sift` et `lightglue` sont trois façons de produire `matches` : un pipeline en utilise une seule. `graphcut` et `dynamic-programming-dijkstra` sont deux méthodes de couture. Une fiche par step dans [docs/steps/](../steps/).

## Ajouter un step

Marche à suivre complète, avec un exemple compilé et testé (en-tête, constructeur, `execute()`, registre, nouveau type et checkpoint, test GoogleTest, fiche) : [steps/AJOUTER_UN_STEP.md](../steps/AJOUTER_UN_STEP.md).

En bref : une classe qui hérite de `StepNs::StepClass`, dont le constructeur par défaut fixe `name`, `parameters`, `inputs` et `outputs` ; une ligne dans `stepRegistry()` ; `REGISTERED_STEP_COUNT` + 1 dans `tests/core/test_pipeline.cpp`. **Le type C++ derrière une clé est le même pour tous les steps**, sinon `get<T>` lève `std::bad_any_cast` à l'exécution.
