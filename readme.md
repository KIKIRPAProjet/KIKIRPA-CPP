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

## 9. La reprise après crash est-elle implémentée ?

### Non, pas actuellement

Il existe des éléments préparatoires qui donnent cette impression, notamment dans `Pipeline.h` :

```cpp
Json exportConfig() const;
```

Et dans `Pipeline.cpp` :

```cpp
Json Pipeline::exportConfig() const
```

Mais cette fonction exporte uniquement la **configuration du pipeline**.

Elle ne sauvegarde pas :

- l’étape courante ;
- les étapes terminées ;
- les données du `DataStore` ;
- les images intermédiaires ;
- les alignements ;
- les positions ;
- les seamlines ;
- la mosaïque ;
- l’état `running`, `paused`, `failed` ou `completed`.

Elle produit donc plutôt un JSON de configuration normalisé, pas un véritable fichier de checkpoint.

De plus, `exportConfig()` n’est pas appelé dans `src/main_test.cpp`.

---

## 10. Pause et reprise : état actuel

Il n’y a actuellement aucun mécanisme pour :

```text
pause()
resume()
cancel()
startFromStep(...)
```

Il n’existe pas non plus de champ tel que :

```json
{
  "currentStep": 3,
  "completedSteps": ["import", "stitching"],
  "status": "paused"
}
```

La méthode `Pipeline::run()` commence toujours à l’index zéro :

```cpp
for (size_t i = 0; i < steps.size(); ++i)
```

Donc même si le premier step a déjà réussi, un nouvel appel à `run()` recommencerait le pipeline depuis le début.

Le commentaire :

```cpp
return false; // on peut reprendre plus tard grâce au JSON
```

exprime probablement l’intention future, mais ce comportement n’est pas encore réalisé par le code actuel.

---

## 11. Différence entre configuration et état d’exécution

C’est la distinction importante à retenir.

### Configuration

Le JSON actuel contient des paramètres comme :

```json
{
  "matcher": "sift",
  "level": 2,
  "threshold": 3.0
}
```

Cela répond à la question :

> Avec quels paramètres dois-je lancer le pipeline ?

### État d’exécution

Pour reprendre après un crash, il faudrait aussi stocker :

```json
{
  "status": "failed",
  "currentStep": 3,
  "completedSteps": ["import", "stitching"],
  "outputs": {
    "tiles": "...",
    "alignments": "..."
  }
}
```

Cela répond à la question :

> Où en était le pipeline et quels résultats puis-je réutiliser ?

Actuellement, seul le premier volet existe.

---

## 12. Ce qu’il faudrait ajouter pour avoir une vraie reprise

Il faudrait probablement séparer deux fichiers ou deux sections.

Par exemple :

```json
{
  "pipeline": {
    "steps": [
      {
        "name": "import",
        "options": []
      }
    ]
  },
  "run": {
    "status": "paused",
    "currentStep": 3,
    "completedSteps": ["import", "stitching"],
    "checkpointDirectory": "output/checkpoints/"
  }
}
```

Il faudrait ensuite ajouter :

1. un état dans `Pipeline` :

   ```cpp
   size_t currentStep;
   PipelineStatus status;
   ```

2. une méthode de démarrage à partir d’un index :

   ```cpp
   bool runFrom(size_t index);
   ```

3. une sérialisation de l’état après chaque step ;

4. une persistance réelle des sorties intermédiaires ;

5. une manière de recharger le `DataStore` après redémarrage ;

6. une gestion explicite de la pause et de l’annulation ;

7. une validation des checkpoints pour éviter de reprendre avec des fichiers incomplets ou incompatibles.

Pour les objets lourds comme `Tiles`, `Alignments` ou `Mosaic`, il ne serait généralement pas suffisant de les mettre directement dans le JSON. Le JSON devrait plutôt référencer des fichiers intermédiaires :

```json
{
  "key": "tiles",
  "path": "checkpoints/tiles.bin",
  "step": "import"
}
```

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
