# Pipeline : script JSON, registre des steps, exécution

Code : `core/Pipeline`, `core/PipelineBuilder`, `core/Step`, `core/Config.h`, `core/DataStore.h`.
Test : `tests/core/test_pipeline.cpp` (`ctest -L unit`).

## Lancer un pipeline

Il n'y a pas d'exécutable en ligne de commande : le pipeline se lance depuis le code (la GUI, les tests).

```cpp
Pipeline p = buildPipelineFromFile("pipeline.json");   // ou buildPipeline(json)
bool ok = p.run([](const std::string& m) { std::cout << m << '\n'; });
const auto& tuiles = p.getData().get<Tiles>("tiles");   // sorties des steps
```

## Format du script

```json
{
  "pipeline": {
    "steps": [
      { "name": "import",
        "options": [ { "type": "string", "key": "path", "value": "images/" } ] },
      { "name": "phase-correlation" }
    ]
  }
}
```

- **`name`** : le nom d'un step du registre (voir plus bas).
- **`options`** : une option absente garde sa **valeur par défaut**. Une option fournie doit exister dans le step, avec le même `type`.

| `type` | Valeurs acceptées |
|---|---|
| `string` | chaîne |
| `int` | entier (2.5 refusé) |
| `float` | nombre (2 accepté) |
| `bool` | `true` / `false` |

- **`pipelineOptions`** (entrées) et **`signature`** (sorties) peuvent figurer dans le JSON, par exemple quand il est écrit par `Pipeline::exportConfig` ou par la GUI. Elles sont **ignorées** à la lecture : c'est le step qui les fixe.

## Contrôles faits par `buildPipeline`

`buildPipeline` lève `std::invalid_argument`, avec le chemin de l'erreur (`pipeline.steps[1] ('graphcut'): …`), si :

- le JSON n'a pas la forme `{ "pipeline": { "steps": [...] } }` ;
- un step est inconnu ;
- une option est inconnue, en double, d'un autre type ou avec une valeur du mauvais type ;
- **une entrée d'un step n'est produite par aucun step précédent**, ou l'est avec un autre type. Par exemple, `graphcut` juste après `import` est refusé : personne ne produit `positions`.

## Exécution

`Pipeline::run` part d'un `DataStore` vide et exécute les steps dans l'ordre. Il s'arrête au premier step qui renvoie `false` **ou lève une exception**, et renvoie `false`. Le message (`outputData` du step, ou le texte de l'exception) va au logger, ou sur `std::cerr` s'il n'y en a pas.

## Reprise, pause, checkpoints

Code : `core/RunState.h`, `core/Checkpoint`. Test : `tests/core/test_reprise.cpp`.

```cpp
RunState r = p.getRunState();
r.checkpointDir = "output/checkpoints";   // active les checkpoints et la reprise
r.from = "phase-correlation";             // facultatif : refaire à partir de ce step
p.setRunState(r);
p.setScriptPath("output/checkpoints/run.json");   // script réécrit après chaque step, relu pour reprendre
p.run(log);
```

Le champ facultatif **`"run"`** du script, à côté de `"pipeline"`, contient deux sortes de valeurs.

**L'état, écrit par le pipeline :**
- `status` : `idle`, `running`, `paused`, `failed` ou `completed` ;
- `completedSteps` : les noms des steps déjà faits, dans l'ordre.

**Les consignes, écrites par l'appelant (la GUI), et remises à leur défaut au démarrage :**
- `skip` (`true` par défaut) : saute les steps déjà faits dont le checkpoint est valide ; `false` : repart de zéro ;
- `from` : le nom du step où (re)démarrer. Il ne peut pas être après le premier step à refaire.

**`checkpointDir`** : dossier des checkpoints. **Vide par défaut, ce qui veut dire ni checkpoint ni reprise.**

**Dans le dossier des checkpoints :**
- Il y a un sous-dossier par step (`00_import/`, `01_phase-correlation/`…), avec les sorties du step et un `manifest.json` écrit en dernier.
- Un checkpoint est valide si le manifest est complet, pour le même step, avec les mêmes options.
- Dès qu'un step est invalide, lui et tous les suivants sont refaits.
- `setScriptPath()` réécrit le script complet (`pipeline` + `run`) après chaque step, par exemple dans `<checkpoints>/run.json`.

**Sérialisation des sorties** (`core/CheckpointTypes.cpp`) : un fichier `<clé>.json` par sortie.

| Type | Ce qui est écrit |
|---|---|
| `Tiles` | les fiches `Tuile` (id, chemins source et TIFF pyramidal, taille, type OpenCV, voisins). **Les pixels ne sont pas recopiés** : à la reprise, le TIFF pyramidal doit toujours exister. |
| `PairMatches` | `idA`, `idB`, direction, points, scores, `dx`, `dy` |
| `Positions` | `tile_id`, position, rotation |
| `Territories` | polygones et trous de chaque tuile |

- Un nouveau type s'ajoute dans `pipelineTypes()`, ou par `Checkpoint::registerType`.
- Un step dont une sortie n'a pas de sérialisation est refait à chaque reprise. Le journal le signale : `checkpoint incomplete`.

**Empreinte des entrées.**
- `Step::inputFingerprint()` décrit ce que le step lit hors du pipeline. Elle est gardée dans le manifest.
- `ImportStep` y met le nom, la taille et la date de chaque image source. Une image ajoutée, retirée ou modifiée périme donc le checkpoint de l'import, et toute la suite est refaite.

**Pause** : `Pipeline::requestPause()` est thread-safe. La pause est prise en compte entre deux steps, et le statut passe à `paused`. Un nouveau `run()` reprend au step suivant.

## Steps enregistrés

| `name` | Classe | Entrées | Sorties |
|---|---|---|---|
| `import` | `steps::ImportStep` | — | `tiles` : `Tiles` |
| `phase-correlation` | `steps::PhaseCorrelationStep` | `tiles` : `Tiles` | `matches` : `PairMatches` |
| `graphcut` | `steps::GraphCutStep` | `tiles` : `Tiles`, `positions` : `Positions` | `territories` : `Territories` |

## Ajouter un step

1. La classe hérite de `Step`. Son **constructeur par défaut** remplit `config` : `name`, `options` avec leurs valeurs par défaut, `pipelineOptions` (entrées) et `signature.pipelineOptions` (sorties). Prendre `PhaseCorrelationStep` pour modèle.
2. `execute()` relit ses options avec `option("clé")`, lit ses entrées avec `data->get<T>("clé")` et écrit ses sorties avec `data->set("clé", …)`. En cas d'échec, il renvoie `false` avec la raison dans `outputData["error"]`.
3. Ajouter une ligne dans `registry()` (`src/Backend/core/PipelineBuilder.cpp`).
4. **Le type C++ derrière une clé doit être le même pour tous les steps** : `tiles` est toujours un `Tiles`, `matches` toujours un `std::vector<PairMatches>`. Sinon, `get<T>` lève `std::bad_any_cast` à l'exécution.
