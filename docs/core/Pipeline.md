# Pipeline : script JSON, registre des steps, exécution

Code : `core/Pipeline`, `core/PipelineBuilder`, `core/Step`, `core/Config.h`, `core/DataStore.h`.
Test : `tests/core/test_pipeline.cpp` (`ctest -L unit`).

## Lancer un pipeline

```bash
stitching_run examples/pipeline.json
```

La commande affiche les steps et leurs options, puis le résultat de chaque step. Elle renvoie 0 si tout a réussi.

Depuis le code :

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
