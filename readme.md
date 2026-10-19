# Notes IA pour comprendre la logique du code (en date du 15/10/26)

Search for `TODO|exportConfig\(|configure\(|DataStore|set\(|get<`

## Résumé en une phrase

La branche a mis en place un **moteur de pipeline configurable par JSON**, avec une liste ordonnée de steps, une validation des dépendances entre steps et un stockage mémoire partagé. En revanche, le mécanisme de **pause/reprise après crash n’est pas encore implémenté** : le JSON actuel contient uniquement la configuration du run, pas son état d’avancement.

---

## 1. Ce qui existe dans la branche

La branche actuelle est :

```text
fe/pipeline-json-parser
```

Elle contient principalement deux évolutions :

- `036e537` — ajout du parseur JSON, du registre de steps et des squelettes de steps ;
- `852c6f8` — déplacement du lanceur de pipeline dans `main_test.cpp`, afin de libérer `main.cpp` pour le frontend.

Le point d’entrée de test est `src/main_test.cpp`.

Le fichier `pipeline.json` est donc bien un **exemple de script de pipeline**.

---

## 2. Structure du JSON

Le fichier contient cette structure générale :

```json
{
  "pipeline": {
    "steps": [
      {
        "name": "import",
        "options": [
          {
            "type": "string",
            "key": "path",
            "value": "images/"
          }
        ]
      }
    ]
  }
}
```

Chaque step contient :

- `name` : nom logique du step ;
- `options` : paramètres réglables par l’utilisateur ;
- éventuellement `pipelineOptions` : entrées consommées ;
- éventuellement `signature.pipelineOptions` : sorties produites.

Dans le JSON actuel, seules les parties `name` et `options` sont renseignées. Les entrées et sorties sont définies côté C++ dans les constructeurs des steps.

Les structures correspondantes sont dans `include/Backend/core/Config.h` :

- `Option`
- `PipelineOption`
- `Signature`
- `StepConfig`
- `PipelineConfig`

Les macros `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE...` permettent de convertir automatiquement entre JSON et structures C++.

---

## 3. Déroulement complet de l’exécution

### Étape 1 — Chargement du fichier

Dans `src/main_test.cpp`, le programme :

1. ouvre `pipeline.json` ;
2. le transforme en objet `Json` avec `Json::parse(file)` ;
3. appelle :

```cpp
Pipeline p = buildPipeline(Json::parse(file));
```

Le chemin par défaut est la racine du projet. Le symbole `PROJECT_DIR` est injecté par CMake dans `CMakeLists.txt`.

---

### Étape 2 — Création des steps

La fonction `buildPipeline` parcourt :

```json
pipeline.steps
```

Pour chaque élément, elle lit le champ `name`.

Le registre interne associe les noms JSON aux classes C++ :

```cpp
"import"              -> ImportStep
"stitching"           -> StitchingStep
"absolutePositioning" -> AbsolutePositioningStep
"intensityCorrection" -> IntensityCorrectionStep
"seamline"            -> SeamlineStep
"fusion"              -> FusionStep
"export"              -> ExportStep
```

Ce registre se trouve dans `src/Backend/core/PipelineBuilder.cpp`.

Par exemple :

```cpp
{"import", [] { return std::make_unique<steps::ImportStep>(); }}
```

Cela permet d’ajouter un nouveau type de step au registre sans modifier la logique générale du pipeline.

Si le JSON contient un nom inconnu, une exception est levée :

```text
unknown step '...'
```

---

### Étape 3 — Application de la configuration JSON

Chaque step possède une configuration par défaut dans son constructeur.

Par exemple, `ImportStep` définit :

```cpp
path   = "images/"
format = "img_C(\\d+)_R(\\d+)\\.tif"
```

Ensuite, `Step::importConfig` remplace les valeurs par celles du JSON.

La logique est :

1. le constructeur crée la configuration par défaut ;
2. le JSON fournit les valeurs personnalisées ;
3. seules les options connues sont acceptées ;
4. une option inconnue provoque une erreur.

Exemple :

```json
{
  "name": "import",
  "options": [
    {
      "type": "string",
      "key": "path",
      "value": "other-images/"
    }
  ]
}
```

La valeur de `path` est remplacée, mais les autres options gardent leur valeur par défaut.

Le champ `type` est stocké dans la configuration, mais il n’est pas réellement utilisé pour valider le type au moment du chargement. La valeur est convertie plus tard lorsque le step appelle par exemple :

```cpp
option("path").get<std::string>()
```

---

## 4. Validation de l’enchaînement des étapes

Chaque step déclare côté C++ :

- les données qu’il consomme ;
- les données qu’il produit.

Cela se trouve dans les constructeurs des fichiers de steps.

Le pipeline logique est actuellement :

```text
Import
  produit : tiles

Stitching
  consomme : tiles
  produit  : alignments

AbsolutePositioning
  consomme : alignments
  produit  : positions

IntensityCorrection
  consomme : tiles
  produit  : tiles

Seamline
  consomme : tiles, positions
  produit  : seams

Fusion
  consomme : tiles, positions, seams
  produit  : mosaic

Export
  consomme : mosaic
```

Visuellement :

```text
images/
   |
   v
import
   |
 tiles
   +--------------------------+
   |                          |
   v                          v
stitching                intensityCorrection
   |                          |
alignments                    tiles
   |
   v
absolutePositioning
   |
positions
   +----------------------+
   |                      |
   v                      v
seamline                fusion
   |                      |
seams                     |
   +----------->----------+
                mosaic
                  |
                  v
                export
```

Cette validation est réalisée dans `PipelineBuilder.cpp`.

Le builder conserve une map :

```cpp
std::map<std::string, std::string> produced;
```

Lorsqu’un step déclare une entrée, le builder vérifie :

1. qu’une étape précédente produit bien cette donnée ;
2. que le type attendu correspond au type produit.

Par exemple, si `fusion` était placé avant `seamline`, le builder refuserait le pipeline car `fusion` nécessite `seams`, qui n’aurait pas encore été produit.

Important : cette validation est faite **avant l’exécution réelle**.

---

## 5. Passage des données entre les steps

Le stockage des résultats intermédiaires est assuré par `DataStore.h`.

Il contient :

```cpp
std::map<std::string, std::any> values;
```

Les données sont donc stockées en mémoire sous forme de couples :

```text
clé -> valeur
```

Par exemple :

```text
"tiles" -> Tiles
"alignments" -> Alignments
"positions" -> Positions
"seams" -> Seams
"mosaic" -> Mosaic
```

Un step peut écrire :

```cpp
data->set("tiles", output.tiles);
```

Et un autre peut lire :

```cpp
data->get<Tiles>("tiles");
```

Le step `import` est actuellement le seul qui écrit effectivement des données dans le `DataStore` :

```cpp
data->set("tiles", output.tiles);
```

Cette logique se trouve dans `src/Backend/steps/ImportStep.cpp`.

Le `DataStore` est donc un bus mémoire partagé entre les étapes, mais il n’est pas sauvegardé sur disque.

---

## 6. Exécution du pipeline

L’exécution séquentielle se trouve dans `Pipeline::run`.

La logique est :

```cpp
for chaque step :
    associer le DataStore au step
    exécuter le step
    si échec :
        arrêter le pipeline
    sinon :
        passer au step suivant
```

En cas de succès :

```text
Step 1/7 OK
Step 2/7 OK
...
Pipeline OK
```

En cas d’échec :

```text
Step 3/7 failed: ...
Pipeline KO
```

La méthode retourne immédiatement `false` lors du premier échec.

Le système de logging existe aussi :

```cpp
using Logger = std::function<void(const std::string&)>;
```

Cela permettra plus tard de connecter le pipeline à une interface graphique, à un fichier de log ou à une console.

---

## 7. Ce qui est réellement implémenté

### Implémenté

#### Parseur JSON

Le JSON est correctement lu et transformé en objets C++.

#### Registre de steps

Les noms du JSON sont associés aux classes C++ via le registre dans `PipelineBuilder.cpp`.

#### Configuration par défaut

Chaque step possède ses options par défaut.

#### Surcharge par JSON

Les valeurs du JSON remplacent les valeurs par défaut.

#### Validation des options

Une option inconnue provoque une erreur.

#### Validation des dépendances

Le builder vérifie que les entrées d’un step sont produites par un step précédent.

#### Import réel des images

`ImportStep` :

- vérifie que le dossier existe ;
- compile la regex ;
- détecte les fichiers correspondants ;
- extrait colonne et ligne ;
- trie les images ;
- charge les images avec OpenCV ;
- conserve les images 16 bits ;
- écrit les tuiles dans le `DataStore`.

#### Exécution séquentielle

Le pipeline parcourt les steps dans l’ordre et s’arrête lorsqu’un step retourne `false`.

---

## 8. Ce qui est encore un squelette

Les steps suivants ont leur configuration et leur signature, mais leur traitement réel n’est pas encore codé :

- `StitchingStep.cpp`
- `AbsolutePositioningStep.cpp`
- `IntensityCorrectionStep.cpp`
- `SeamlineStep.cpp`
- `FusionStep.cpp`
- `ExportStep.cpp`

Ils contiennent actuellement :

```cpp
bool ...::execute() {
    // TODO
    return true;
}
```

Cela signifie qu’ils déclarent ce qu’ils sont censés consommer et produire, mais ne réalisent pas encore vraiment ces opérations.

En particulier, la majorité d’entre eux :

- ne lisent pas encore leurs entrées dans le `DataStore` ;
- ne produisent pas leurs sorties ;
- ne remplissent pas `outputData` ;
- ne sauvegardent pas leurs résultats.

---

## 9. Générer `pipeline.json` : `generate_pipeline` (ébauche)

`pipeline.json` n'est pas écrit à la main : il est produit par un programme backend, à partir des arguments donnés en console.
Les steps connaissent déjà leurs options et leurs valeurs par défaut (constructeurs) : le générateur les instancie
(`createStep`), applique les arguments, **valide avec `buildPipeline`** (steps/options connus, enchaînement des données),
et n'écrit le fichier que si tout est valide.

```text
generate_pipeline --list                       # steps, options, types, valeurs par défaut
generate_pipeline                              # tout par défaut -> pipeline.json
generate_pipeline seamline.seamFinder=graphcut stitching.ratio=0.8 export.path=output/mosaic.tif
generate_pipeline -i                           # saisie interactive (Entrée = garder la valeur proposée)
generate_pipeline --steps import,stitching     # sous-ensemble de steps (l'enchaînement reste validé)
generate_pipeline -o mon_script.json ...       # fichier de sortie (défaut : pipeline.json à la racine du projet)
```

- Syntaxe `step.option=valeur` (le préfixe `step.` est obligatoire : `level` existe dans `stitching` et dans `seamline`).
- La valeur est convertie selon le type déclaré de l'option (`int`, `float`, `bool`, `string`) ; une valeur invalide est refusée.
- Le récapitulatif affiché marque d'une `*` ce qui diffère des valeurs par défaut.
- Le script produit ne contient que `name` + `options` par step (le champ `run` de la section 10 est ajouté par l'exécution).
- Pour exposer une nouvelle option, il suffit de la déclarer dans le step : le générateur la prend en compte automatiquement.

Limite actuelle : les valeurs autorisées d'une option (ex. `graphcut`, `voronoi`) ne sont pas déclarées dans le code ; le
générateur ne peut donc pas les proposer ni les valider (le type seul est contrôlé).

---

## 10. Reprise après crash / pause / reprise à une étape intermédiaire (implémenté)

Le script JSON est **généré dynamiquement** : le frontend écrit `pipeline` (+ consignes dans `run`), puis le backend
réécrit le fichier (de façon atomique) **après chaque step** via `Pipeline::setScriptPath(...)`.

```json
{
  "pipeline": { "steps": [ ... ] },
  "run": {
    "status": "failed",
    "completedSteps": ["import", "stitching"],
    "skip": true,
    "from": "",
    "checkpointDir": "output/checkpoints"
  }
}
```

| Champ | Rôle | Qui l'écrit |
|---|---|---|
| `status` | `idle`, `running`, `paused`, `failed`, `completed` | backend |
| `completedSteps` | noms des steps terminés (préfixe du pipeline) | backend |
| `skip` | `true` : on saute les steps déjà faits ; `false` : on repart de zéro | frontend (consigne) |
| `from` | nom d'un step où (re)démarrer | frontend (consigne) |
| `checkpointDir` | dossier des résultats intermédiaires | frontend |

`skip` et `from` sont des **consignes consommées au démarrage** : le backend les remet à leur défaut dans le script qu'il réécrit,
pour que relancer ce script reprenne là où on en est (et pas depuis le début).

### Choix de conception (réponse au `todo.md`)

- **Liste de noms** (`completedSteps`) plutôt qu'index : lisible, et vérifiée contre le pipeline (nom ET ordre). L'index se déduit.
- **Un checkpoint par step** (`NN_nom/`, voir `core/Checkpoint.h`) et pas par clé : `intensityCorrection` consomme et produit
  `tiles`; avec un dossier par step, reprendre *avant* lui redonne les tuiles d'origine.
- Le fichier `manifest.json` d'un step est écrit **en dernier** : sa présence = checkpoint complet. L'ordre est toujours
  « sauver les sorties, puis déclarer le step terminé ».
- Un checkpoint n'est **valide que si les options du step n'ont pas changé**. Dès qu'un step est invalide, lui et tous les suivants
  sont refaits (rembobinage automatique).

### Algorithme de `Pipeline::run()`

1. `start` = nombre de steps de tête valides (nom concordant + checkpoint valide). `from` permet de revenir plus tôt,
   jamais d'aller au-delà (sinon `std::invalid_argument`). `skip=false` => `start = 0`.
2. Les checkpoints des steps >= `start` sont supprimés (périmés).
3. Le `DataStore` est restauré : pour chaque clé, la dernière version écrite par un step < `start`.
4. Exécution des steps restants ; un step qui échoue **ou lève une exception** => `status=failed`.
5. `requestPause()` (thread-safe) : arrêt propre entre deux steps => `status=paused`.

Un crash brutal laisse `status=running` avec `completedSteps` à jour : la reprise fonctionne de la même manière.

### Lancer / reprendre (`stitching_test`)

```text
stitching_test                       # pipeline.json, état écrit dans output/run.json
stitching_test output/run.json       # reprise après arrêt/crash
stitching_test output/run.json --from seamline   # refaire à partir d'un step
stitching_test output/run.json --restart         # tout refaire
```

### Limites actuelles

- Seul le type `Tiles` a un sérialiseur (registre dans `Checkpoint.cpp`). Quand on implémentera `Alignments`, `Positions`,
  `Seams`, `Mosaic`, **il faut ajouter leur entrée au registre** : sinon le checkpoint du step est déclaré incomplet
  (avertissement dans le log) et ce step est refait à la reprise.
- Les tuiles sont recopiées en TIFF dans le checkpoint (même pour `import`). Optimisation possible : référencer les fichiers sources.
- Un step en cours n'est pas interruptible : la pause et la reprise se font à la granularité du step.

---

## 11. Anciennes limites (avant ce chantier)

`Pipeline::run()` repartait toujours de l'index 0 et `exportConfig()` n'exportait que la configuration. Ce n'est plus le cas :
voir section 10. `exportConfig()` reste disponible (configuration seule) ; `exportScript()` exporte `pipeline` + `run`.

---

## 13. Point particulier : `Pipeline::configure`

Il existe une seconde manière de configurer un pipeline dans `Pipeline.cpp`.

Cette méthode :

```cpp
void Pipeline::configure(const Json& cfg)
```

suppose que les steps ont déjà été créés et que leur nombre correspond exactement à celui du JSON :

```cpp
if (config.steps.size() != steps.size())
    throw ...
```

Elle applique ensuite la configuration à chaque step existant.

Mais le chemin actuellement utilisé par le lanceur est différent :

```text
JSON
  -> buildPipeline()
  -> création des steps
  -> importConfig()
  -> validation
  -> run()
```

`Pipeline::configure()` n’est donc pas utilisée par `main_test.cpp`.

Elle semble être une API préparée pour un autre usage, par exemple :

```cpp
Pipeline p;
p.add(std::make_unique<ImportStep>());
p.add(std::make_unique<StitchingStep>());
p.configure(json);
p.run();
```

---

## 14. Ce que fait actuellement le lancement

La séquence actuelle est donc :

```text
main_test.cpp
    |
    v
lecture de pipeline.json
    |
    v
Json::parse()
    |
    v
buildPipeline()
    |
    +-- recherche du step dans le registre
    +-- création de la classe C++
    +-- application des options JSON
    +-- validation des entrées/sorties
    |
    v
Pipeline::run()
    |
    +-- ImportStep
    +-- StitchingStep
    +-- AbsolutePositioningStep
    +-- IntensityCorrectionStep
    +-- SeamlineStep
    +-- FusionStep
    +-- ExportStep
```

À ce jour :

- le chargement JSON fonctionne ;
- la construction du pipeline fonctionne ;
- la validation structurelle fonctionne ;
- l’import fonctionne ;
- les autres traitements sont encore des squelettes ;
- la reprise sur checkpoint n’existe pas encore.

À noter également : le répertoire `build/` présent localement ne contient pas actuellement `stitching_test`, ce qui indique qu’il est probablement antérieur à l’ajout de la nouvelle cible CMake. Le code source prévoit bien cette cible dans `CMakeLists.txt`.

---

## Conclusion

Ton interprétation correspond à l’objectif visé, mais pas encore à l’état actuel du code.

La branche a construit la base nécessaire :

- pipeline déclaré par JSON ;
- paramètres séparés du code ;
- steps créés dynamiquement ;
- validation des dépendances ;
- données intermédiaires partagées en mémoire ;
- arrêt propre en cas d’échec ;
- export possible de la configuration.

Mais il manque encore la partie essentielle pour obtenir une vraie pipeline reprenable :

- persistance de l’état ;
- index de l’étape courante ;
- sauvegarde des résultats intermédiaires ;
- restauration du `DataStore` ;
- reprise à partir d’une étape donnée ;
- pause contrôlée ;
- distinction entre configuration et état du run.
