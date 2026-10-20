# KIKIRPA-CPP : pipeline de stitching d'images

## Table des matières<!-- omit from toc -->

- [KIKIRPA-CPP : pipeline de stitching d'images](#kikirpa-cpp--pipeline-de-stitching-dimages)
  - [Introduction](#introduction)
  - [Explication du code par Ludovic (après modification pour ajout de resume)](#explication-du-code-par-ludovic-après-modification-pour-ajout-de-resume)
    - [1. Prérequis](#1-prérequis)
    - [2. Compiler](#2-compiler)
    - [3. Démarrage rapide](#3-démarrage-rapide)
    - [4. Préparer les images](#4-préparer-les-images)
    - [5. Générer `pipeline.json` : `generate_pipeline`](#5-générer-pipelinejson--generate_pipeline)
      - [Options disponibles (valeurs par défaut)](#options-disponibles-valeurs-par-défaut)
    - [6. Exécuter : `stitching_test`](#6-exécuter--stitching_test)
    - [7. Reprendre après un arrêt, un échec ou un crash](#7-reprendre-après-un-arrêt-un-échec-ou-un-crash)
      - [Le champ `run` du fichier d'état](#le-champ-run-du-fichier-détat)
    - [8. Dépannage](#8-dépannage)
    - [9. État actuel et limites](#9-état-actuel-et-limites)
    - [10. Architecture](#10-architecture)
      - [Déroulement de `Pipeline::run()`](#déroulement-de-pipelinerun)
      - [Checkpoints sur disque](#checkpoints-sur-disque)
    - [11. Ajouter ou compléter un step](#11-ajouter-ou-compléter-un-step)
    - [12. Historique](#12-historique)
  - [Notes IA du code de base cloné depuis la branche `fe/pipeline-json-parser` par Mathias (en date du 15/10/26)](#notes-ia-du-code-de-base-cloné-depuis-la-branche-fepipeline-json-parser-par-mathias-en-date-du-151026)
    - [Résumé en une phrase](#résumé-en-une-phrase)
    - [1. Ce qui existe dans la branche](#1-ce-qui-existe-dans-la-branche)
    - [2. Structure du JSON](#2-structure-du-json)
    - [3. Déroulement complet de l’exécution](#3-déroulement-complet-de-lexécution)
      - [Étape 1 — Chargement du fichier](#étape-1--chargement-du-fichier)
      - [Étape 2 — Création des steps](#étape-2--création-des-steps)
      - [Étape 3 — Application de la configuration JSON](#étape-3--application-de-la-configuration-json)
    - [4. Validation de l’enchaînement des étapes](#4-validation-de-lenchaînement-des-étapes)
    - [5. Passage des données entre les steps](#5-passage-des-données-entre-les-steps)
    - [6. Exécution du pipeline](#6-exécution-du-pipeline)
    - [7. Ce qui est réellement implémenté](#7-ce-qui-est-réellement-implémenté)
      - [Implémenté](#implémenté)
        - [Parseur JSON](#parseur-json)
        - [Registre de steps](#registre-de-steps)
        - [Configuration par défaut](#configuration-par-défaut)
        - [Surcharge par JSON](#surcharge-par-json)
        - [Validation des options](#validation-des-options)
        - [Validation des dépendances](#validation-des-dépendances)
        - [Import réel des images](#import-réel-des-images)
        - [Exécution séquentielle](#exécution-séquentielle)
    - [8. Ce qui est encore un squelette](#8-ce-qui-est-encore-un-squelette)
    - [9. La reprise après crash est-elle implémentée ?](#9-la-reprise-après-crash-est-elle-implémentée-)
      - [Non, pas actuellement](#non-pas-actuellement)
    - [10. Pause et reprise : état actuel](#10-pause-et-reprise--état-actuel)
    - [11. Différence entre configuration et état d’exécution](#11-différence-entre-configuration-et-état-dexécution)
      - [Configuration](#configuration)
    - [État d’exécution](#état-dexécution)
    - [12. Ce qu’il faudrait ajouter pour avoir une vraie reprise](#12-ce-quil-faudrait-ajouter-pour-avoir-une-vraie-reprise)
    - [13. Point particulier : `Pipeline::configure`](#13-point-particulier--pipelineconfigure)
    - [14. Ce que fait actuellement le lancement](#14-ce-que-fait-actuellement-le-lancement)
    - [Conclusion](#conclusion)

## Introduction

Assemble une grille de tuiles (TIFF 16 bits) en une mosaïque, via un **pipeline de steps** décrit par un fichier JSON.
Le fichier JSON est **généré** par un programme backend (`generate_pipeline`), puis **exécuté** par le moteur de pipeline,
qui sait reprendre après un arrêt, un crash ou depuis une étape choisie.

> **État du projet** : le moteur (génération, exécution, checkpoints, reprise) fonctionne. Seul le step `import` est
> réellement implémenté ; `stitching`, `absolutePositioning`, `intensityCorrection`, `seamline`, `fusion` et `export`
> sont des squelettes qui réussissent sans rien calculer. Voir [section 9](#9-état-actuel-et-limites).

---

<details>
  <summary><b>Explication du code par Ludovic (après modification pour ajout de resume)</b></summary>

## Explication du code par Ludovic (après modification pour ajout de resume)

### 1. Prérequis

| Outil                      | Remarque                                                                                   |
| -------------------------- | ------------------------------------------------------------------------------------------ |
| Compilateur C++17          | g++, clang ou MSVC                                                                         |
| CMake ≥ 3.16               |                                                                                            |
| OpenCV (4.x)               | obligatoire                                                                                |
| nlohmann_json ≥ 3          | cherché sur le système ; sinon téléchargé par CMake (réseau nécessaire à la configuration) |
| Qt 6 (Widgets, Concurrent) | optionnel, pour le futur frontend ; ignoré s'il est absent                                 |

Exemple sous Ubuntu : `sudo apt install cmake g++ libopencv-dev nlohmann-json3-dev`

### 2. Compiler

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Exécutables produits dans `build/` :

| Exécutable           | Rôle                                                                                    |
| -------------------- | --------------------------------------------------------------------------------------- |
| `generate_pipeline`  | **génère** `pipeline.json` à partir d'arguments de console                              |
| `stitching_test`     | **exécute** un script JSON (avec checkpoints et reprise)                                |
| `stitching`          | réservé à l'application finale, **vide pour l'instant**                                 |
| `stitching_frontend` | interface Qt, construite seulement si Qt 6 est présent, **sans sources pour l'instant** |

Les nouveaux fichiers `.cpp` de `src/Backend/` sont pris en compte automatiquement (relancer CMake).

### 3. Démarrage rapide

```bash
# 1. Mettre les tuiles dans images/  (voir section 4 pour le nommage)
# 2. Générer le script
./build/generate_pipeline
# 3. Lancer
./build/stitching_test
```

Sans argument, `generate_pipeline` écrit `pipeline.json` à la racine du projet et `stitching_test` le lit de là
(et se place à la racine du projet, où se trouve `images/`). Le résultat de chaque step est affiché au fur et à mesure,
et l'état du run est écrit dans `output/run.json`.

### 4. Préparer les images

Les tuiles doivent être dans un même dossier (par défaut `images/`, relatif au dossier **courant**) et nommées selon le
format `import.format`, une expression régulière avec **deux groupes** : colonne puis ligne.

```text
images/img_C00_R00.tif   images/img_C01_R00.tif   images/img_C02_R00.tif
images/img_C00_R01.tif   images/img_C01_R01.tif   images/img_C02_R01.tif
```

Les images sont lues telles quelles (`IMREAD_UNCHANGED`), donc le 16 bits est conservé, et triées par ligne puis par colonne.
Si les fichiers ont un autre nommage, il suffit de changer le format :

```bash
./build/generate_pipeline 'import.format=tile_(\d+)_(\d+)\.png' import.path=photos/
```

### 5. Générer `pipeline.json` : `generate_pipeline`

Chaque step connaît ses options et leurs valeurs par défaut ; le générateur les applique, **valide** l'ensemble
(steps et options connus, enchaînement des données) et **n'écrit le fichier que s'il est valide**.

```text
generate_pipeline [options] [step.option=valeur ...]

  --list              affiche les steps, leurs options, leurs types et leurs valeurs par défaut
  -i, --interactive   demande chaque option (Entrée = garder la valeur proposée)
  --steps a,b,c       ne garde que ces steps, dans cet ordre (défaut : tous)
  -o <fichier>        fichier de sortie (défaut : pipeline.json à la racine du projet)
  -h, --help          aide
```

Exemples :

```bash
./build/generate_pipeline --list
./build/generate_pipeline seamline.seamFinder=voronoi stitching.ratio=0.8 export.path=output/mosaic.tif
./build/generate_pipeline -i
./build/generate_pipeline -o scripts/test_rapide.json --steps import,stitching
```

Règles :

- La syntaxe est `step.option=valeur`. Le préfixe `step.` est obligatoire (`level` existe dans `stitching` **et** dans `seamline`).
- La valeur est convertie selon le type de l'option. Booléens acceptés : `true/false`, `1/0`, `yes/no`, `on/off`.
- Mettre entre guillemets simples les valeurs contenant des caractères spéciaux du shell (regex, etc.).
- Le récapitulatif affiché marque d'une `*` les options différentes de leur valeur par défaut.
- Seul le **type** des valeurs est contrôlé : `seamline.seamFinder=nimporte_quoi` est accepté.

#### Options disponibles (valeurs par défaut)

| Step                  | Option          | Type   | Défaut                   |
| --------------------- | --------------- | ------ | ------------------------ |
| `import`              | `path`          | string | `images/`                |
|                       | `format`        | string | `img_C(\d+)_R(\d+)\.tif` |
| `stitching`           | `matcher`       | string | `sift`                   |
|                       | `rejector`      | string | `ransac`                 |
|                       | `level`         | int    | `2`                      |
|                       | `ratio`         | float  | `0.75`                   |
|                       | `threshold`     | float  | `3.0`                    |
| `absolutePositioning` | `referenceTile` | int    | `0`                      |
| `intensityCorrection` | `enabled`       | bool   | `true`                   |
| `seamline`            | `seamFinder`    | string | `graphcut`               |
|                       | `level`         | int    | `1`                      |
| `fusion`              | `blending`      | string | `multiband`              |
|                       | `bands`         | int    | `5`                      |
| `export`              | `path`          | string | `output/mosaic.tif`      |
|                       | `format`        | string | `bigtiff`                |
|                       | `bits`          | int    | `16`                     |
|                       | `blockSize`     | int    | `4096`                   |
|                       | `keepIcc`       | bool   | `true`                   |
|                       | `pyramid`       | bool   | `true`                   |

`generate_pipeline --list` reste la référence : il lit les valeurs directement dans le code des steps.

### 6. Exécuter : `stitching_test`

```text
stitching_test [script.json] [--from <step>] [--restart]

  (sans argument)   exécute pipeline.json, depuis la racine du projet
  script.json       exécute ce script (le dossier courant sert de base aux chemins relatifs : images/, output/...)
  --from <step>     (re)démarre à ce step
  --restart         ignore les checkpoints et repart de zéro
```

Pendant l'exécution :

- l'état du run est réécrit dans **`output/run.json`** après chaque step (le script d'origine n'est jamais modifié) ;
- les résultats intermédiaires sont sauvegardés dans **`output/checkpoints/`** ;
- le code de retour est `0` si tout réussit, `1` sinon.

### 7. Reprendre après un arrêt, un échec ou un crash

Après un arrêt (erreur, `Ctrl+C`, plantage, coupure), **relancer le fichier d'état** :

```bash
./build/stitching_test output/run.json
```

Les steps déjà terminés sont sautés (leurs résultats sont rechargés depuis les checkpoints) et l'exécution reprend au
premier step non terminé.

| Je veux…                                        | Commande                                                 |
| ----------------------------------------------- | -------------------------------------------------------- |
| reprendre là où ça s'est arrêté                 | `./build/stitching_test output/run.json`                 |
| refaire à partir d'un step (les suivants aussi) | `./build/stitching_test output/run.json --from seamline` |
| tout refaire depuis le début                    | `./build/stitching_test output/run.json --restart`       |

À savoir :

- `--from <step>` ne peut pas **dépasser** le premier step non terminé (les données manqueraient) : le programme refuse avec un message.
- Si les **options** d'un step terminé ont changé dans le script, son checkpoint est considéré périmé : il est refait, ainsi que tous les suivants.
- Un checkpoint absent ou corrompu est traité de la même façon : l'exécution reprend à ce step.
- Les **images sources** ne sont pas surveillées : si tu les remplaces sans changer les options, utilise `--restart`.
- Un step en cours ne peut pas être interrompu : une reprise se fait toujours au début d'un step.

#### Le champ `run` du fichier d'état

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

| Champ            | Rôle                                                               | Écrit par     |
| ---------------- | ------------------------------------------------------------------ | ------------- |
| `status`         | `idle`, `running`, `paused`, `failed`, `completed`                 | backend       |
| `completedSteps` | noms des steps terminés, dans l'ordre                              | backend       |
| `skip`           | `true` : on saute les steps terminés ; `false` : on repart de zéro | consigne      |
| `from`           | nom du step où (re)démarrer                                        | consigne      |
| `checkpointDir`  | dossier des résultats intermédiaires                               | configuration |

`skip` et `from` sont des **consignes** : elles sont lues au démarrage puis remises à leur valeur par défaut dans le fichier
réécrit. C'est ce qui garantit que relancer `output/run.json` reprend là où on en est.

### 8. Dépannage

| Message                                                       | Cause et solution                                                                                                                  |
| ------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| `not a directory: images/`                                    | le dossier `import.path` n'existe pas **depuis le dossier courant**. Lancer depuis la racine du projet ou donner un chemin absolu. |
| `no file matching '...' in ...`                               | aucun fichier ne correspond à `import.format`. Vérifier le nommage (section 4).                                                    |
| `format '...' needs 2 groups`                                 | la regex d'import doit avoir deux groupes capturants : colonne, ligne.                                                             |
| `invalid format '...'`                                        | la regex d'import est invalide.                                                                                                    |
| `step 'x': unknown option 'y'`                                | option inconnue dans le script. Régénérer le script avec `generate_pipeline`.                                                      |
| `'step.option': step '...' is not in the pipeline`            | option donnée pour un step exclu par `--steps`.                                                                                    |
| `cannot start at 'x': step N must be completed first`         | `--from` dépasse le premier step non terminé.                                                                                      |
| `... is not a valid int/float/bool for option`                | valeur du mauvais type dans `generate_pipeline`.                                                                                   |
| `warning: checkpoint incomplete (no serializer for type ...)` | un step produit un type de donnée sans sérialiseur : il sera refait à la reprise (section 10).                                     |
| un step « réussit » sans rien produire                        | normal pour l'instant : seuls `import` et le moteur sont implémentés.                                                              |

### 9. État actuel et limites

**Fonctionne et testé** (sur un pipeline squelette avec tuiles synthétiques) : génération du script, validation, exécution
séquentielle, checkpoints par step, reprise après crash, reprise depuis un step, pause, `--restart`.

**Pas encore fait** :

- les six steps après `import` (ils renvoient `true` sans calcul) ;
- un sérialiseur de checkpoint pour chaque type de donnée à venir (seul `Tiles` en a un) ;
- les valeurs autorisées par option (`graphcut`, `voronoi`…) ne sont pas déclarées, donc non validées ;
- `main.cpp` (exécutable final `stitching`) et le frontend Qt ;
- `Option.type` n'est pas validé au chargement d'un script ;
- les checkpoints recopient les tuiles en TIFF (coûteux en disque sur de gros jeux de données) ;
- `getRunState()` n'est pas protégé contre un accès concurrent pendant `run()`.

La liste de suivi est dans [`todo.md`](todo.md).

---

### 10. Architecture

```text
include/Backend/core/    Pipeline, Step, DataStore, PipelineBuilder, Config, RunState, Checkpoint, Types, Tuile
include/Backend/steps/   un fichier .h par step
src/Backend/core/        implémentations du moteur
src/Backend/steps/       un .cpp par step
src/main_generate.cpp    generate_pipeline
src/main_test.cpp        stitching_test
src/main.cpp             (vide) application finale
```

- **`Step`** : classe abstraite, `execute()` est virtuelle pure. Sa config (`name`, `options`, `signature`) est un JSON.
  `importConfig()` fusionne les valeurs du script dans les défauts du constructeur et **rejette les options inconnues**.
- **`DataStore`** : `map<string, std::any>` en mémoire, le bus de données entre steps (`set`, `get<T>`, `has`).
- **`PipelineBuilder`** : registre `nom → classe`. `buildPipeline(json)` crée les steps et **vérifie l'enchaînement** :
  chaque entrée d'un step doit être produite, avec le bon type, par un step précédent.
  Expose aussi `createStep(nom)` et `defaultStepOrder()`, utilisés par le générateur.
- **`Pipeline`** : liste ordonnée de steps + `DataStore` + `RunState`. `run()` exécute avec reprise (voir ci-dessous).
- **`Checkpoint`** : sauvegarde et rechargement des sorties de chaque step.

#### Déroulement de `Pipeline::run()`

1. **Point de départ** : nombre de steps de tête dont le nom concorde avec `completedSteps` **et** dont le checkpoint est
   valide (complet, mêmes options). `from` permet de revenir plus tôt, jamais plus loin. `skip=false` force le départ à 0.
2. **Nettoyage** : les checkpoints des steps à refaire sont supprimés.
3. **Restauration** du `DataStore` : pour chaque clé, la dernière version écrite par un step déjà terminé.
4. **Exécution** des steps restants. Après chaque step réussi : sauvegarde de ses sorties **puis** ajout à
   `completedSteps` **puis** réécriture du script (toujours dans cet ordre). Un step qui échoue ou lève une exception
   donne `status=failed`. Une demande de pause (`requestPause()`, thread-safe) arrête proprement entre deux steps.

#### Checkpoints sur disque

```text
output/checkpoints/
  00_import/manifest.json    ← écrit EN DERNIER : sa présence = checkpoint complet
  00_import/tiles.json, tiles_0000.tif, tiles_0001.tif, ...
  01_stitching/...
```

Un dossier **par step** (et non par clé de données) : `intensityCorrection` consomme et produit `tiles` ; avec un dossier par
step, reprendre avant lui redonne les tuiles d'origine. Toutes les écritures de fichiers d'état sont atomiques
(fichier temporaire puis `rename`).

### 11. Ajouter ou compléter un step

1. Implémenter `execute()` dans `src/Backend/steps/XxxStep.cpp` : lire les entrées avec `data->get<T>("clé")`, écrire les
   sorties avec `data->set("clé", valeur)`, retourner `false` en cas d'échec (renseigner `outputData["error"]`).
2. Déclarer ses **options** (avec valeurs par défaut) et sa **signature** (entrées/sorties : type et clé) dans le constructeur :
   `generate_pipeline` et la validation s'en servent automatiquement.
3. Si le step produit un **nouveau type** de donnée, ajouter son sérialiseur dans le registre de `Checkpoint.cpp`
   (fonctions `save` et `load`, voir `saveTiles` / `loadTiles`). Sans lui, la reprise refera ce step et les suivants.
4. Pour un **nouveau step** : l'ajouter au registre de `PipelineBuilder.cpp` et à `defaultStepOrder()`.

Vérifier ensuite :

```bash
./build/generate_pipeline --list      # les options apparaissent
./build/stitching_test                # le step s'exécute
# simuler un arrêt : éditer output/run.json (ne garder que les premiers steps dans completedSteps, status "running")
./build/stitching_test output/run.json
```

### 12. Historique

Le champ `run`, les checkpoints, la reprise et le générateur `generate_pipeline` ont été ajoutés après la mise en place du
parseur JSON et des squelettes de steps (branche `fe/pipeline-json-parser`).

</details>

<details>
  <summary><b>Notes IA du code de base cloné depuis la branche `fe/pipeline-json-parser` par Mathias (en date du 15/10/26)</b></summary>

## Notes IA du code de base cloné depuis la branche `fe/pipeline-json-parser` par Mathias (en date du 15/10/26)

### Résumé en une phrase

La branche a mis en place un **moteur de pipeline configurable par JSON**, avec une liste ordonnée de steps, une validation des dépendances entre steps et un stockage mémoire partagé. En revanche, le mécanisme de **pause/reprise après crash n’est pas encore implémenté** : le JSON actuel contient uniquement la configuration du run, pas son état d’avancement.

---

### 1. Ce qui existe dans la branche

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

### 2. Structure du JSON

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

### 3. Déroulement complet de l’exécution

#### Étape 1 — Chargement du fichier

Dans `src/main_test.cpp`, le programme :

1. ouvre `pipeline.json` ;
2. le transforme en objet `Json` avec `Json::parse(file)` ;
3. appelle :

```cpp
Pipeline p = buildPipeline(Json::parse(file));
```

Le chemin par défaut est la racine du projet. Le symbole `PROJECT_DIR` est injecté par CMake dans `CMakeLists.txt`.

---

#### Étape 2 — Création des steps

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

#### Étape 3 — Application de la configuration JSON

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

### 4. Validation de l’enchaînement des étapes

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

### 5. Passage des données entre les steps

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

### 6. Exécution du pipeline

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

### 7. Ce qui est réellement implémenté

#### Implémenté

##### Parseur JSON

Le JSON est correctement lu et transformé en objets C++.

##### Registre de steps

Les noms du JSON sont associés aux classes C++ via le registre dans `PipelineBuilder.cpp`.

##### Configuration par défaut

Chaque step possède ses options par défaut.

##### Surcharge par JSON

Les valeurs du JSON remplacent les valeurs par défaut.

##### Validation des options

Une option inconnue provoque une erreur.

##### Validation des dépendances

Le builder vérifie que les entrées d’un step sont produites par un step précédent.

##### Import réel des images

`ImportStep` :

- vérifie que le dossier existe ;
- compile la regex ;
- détecte les fichiers correspondants ;
- extrait colonne et ligne ;
- trie les images ;
- charge les images avec OpenCV ;
- conserve les images 16 bits ;
- écrit les tuiles dans le `DataStore`.

##### Exécution séquentielle

Le pipeline parcourt les steps dans l’ordre et s’arrête lorsqu’un step retourne `false`.

---

### 8. Ce qui est encore un squelette

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

### 9. La reprise après crash est-elle implémentée ?

#### Non, pas actuellement

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

### 10. Pause et reprise : état actuel

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

### 11. Différence entre configuration et état d’exécution

C’est la distinction importante à retenir.

#### Configuration

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

### 12. Ce qu’il faudrait ajouter pour avoir une vraie reprise

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

### 13. Point particulier : `Pipeline::configure`

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

### 14. Ce que fait actuellement le lancement

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

### Conclusion

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

</details>
