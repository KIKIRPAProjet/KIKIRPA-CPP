# KIKIRPA-CPP : pipeline de stitching d'images

Assemble une grille de tuiles (TIFF 16 bits) en une mosaïque, via un **pipeline de steps** décrit par un fichier JSON.
Le fichier JSON est **généré** par un programme backend (`generate_pipeline`), puis **exécuté** par le moteur de pipeline,
qui sait reprendre après un arrêt, un crash ou depuis une étape choisie.

> **État du projet** : le moteur (génération, exécution, checkpoints, reprise) fonctionne. Seul le step `import` est
> réellement implémenté ; `stitching`, `absolutePositioning`, `intensityCorrection`, `seamline`, `fusion` et `export`
> sont des squelettes qui réussissent sans rien calculer. Voir [section 9](#9-état-actuel-et-limites).

---

## 1. Prérequis

| Outil | Remarque |
|---|---|
| Compilateur C++17 | g++, clang ou MSVC |
| CMake ≥ 3.16 | |
| OpenCV (4.x) | obligatoire |
| nlohmann_json ≥ 3 | cherché sur le système ; sinon téléchargé par CMake (réseau nécessaire à la configuration) |
| Qt 6 (Widgets, Concurrent) | optionnel, pour le futur frontend ; ignoré s'il est absent |

Exemple sous Ubuntu : `sudo apt install cmake g++ libopencv-dev nlohmann-json3-dev`

## 2. Compiler

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Exécutables produits dans `build/` :

| Exécutable | Rôle |
|---|---|
| `generate_pipeline` | **génère** `pipeline.json` à partir d'arguments de console |
| `stitching_test` | **exécute** un script JSON (avec checkpoints et reprise) |
| `stitching` | réservé à l'application finale, **vide pour l'instant** |
| `stitching_frontend` | interface Qt, construite seulement si Qt 6 est présent, **sans sources pour l'instant** |

Les nouveaux fichiers `.cpp` de `src/Backend/` sont pris en compte automatiquement (relancer CMake).

## 3. Démarrage rapide

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

## 4. Préparer les images

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

## 5. Générer `pipeline.json` : `generate_pipeline`

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

### Options disponibles (valeurs par défaut)

| Step | Option | Type | Défaut |
|---|---|---|---|
| `import` | `path` | string | `images/` |
| | `format` | string | `img_C(\d+)_R(\d+)\.tif` |
| `stitching` | `matcher` | string | `sift` |
| | `rejector` | string | `ransac` |
| | `level` | int | `2` |
| | `ratio` | float | `0.75` |
| | `threshold` | float | `3.0` |
| `absolutePositioning` | `referenceTile` | int | `0` |
| `intensityCorrection` | `enabled` | bool | `true` |
| `seamline` | `seamFinder` | string | `graphcut` |
| | `level` | int | `1` |
| `fusion` | `blending` | string | `multiband` |
| | `bands` | int | `5` |
| `export` | `path` | string | `output/mosaic.tif` |
| | `format` | string | `bigtiff` |
| | `bits` | int | `16` |
| | `blockSize` | int | `4096` |
| | `keepIcc` | bool | `true` |
| | `pyramid` | bool | `true` |

`generate_pipeline --list` reste la référence : il lit les valeurs directement dans le code des steps.

## 6. Exécuter : `stitching_test`

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

## 7. Reprendre après un arrêt, un échec ou un crash

Après un arrêt (erreur, `Ctrl+C`, plantage, coupure), **relancer le fichier d'état** :

```bash
./build/stitching_test output/run.json
```

Les steps déjà terminés sont sautés (leurs résultats sont rechargés depuis les checkpoints) et l'exécution reprend au
premier step non terminé.

| Je veux… | Commande |
|---|---|
| reprendre là où ça s'est arrêté | `./build/stitching_test output/run.json` |
| refaire à partir d'un step (les suivants aussi) | `./build/stitching_test output/run.json --from seamline` |
| tout refaire depuis le début | `./build/stitching_test output/run.json --restart` |

À savoir :

- `--from <step>` ne peut pas **dépasser** le premier step non terminé (les données manqueraient) : le programme refuse avec un message.
- Si les **options** d'un step terminé ont changé dans le script, son checkpoint est considéré périmé : il est refait, ainsi que tous les suivants.
- Un checkpoint absent ou corrompu est traité de la même façon : l'exécution reprend à ce step.
- Les **images sources** ne sont pas surveillées : si tu les remplaces sans changer les options, utilise `--restart`.
- Un step en cours ne peut pas être interrompu : une reprise se fait toujours au début d'un step.

### Le champ `run` du fichier d'état

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

| Champ | Rôle | Écrit par |
|---|---|---|
| `status` | `idle`, `running`, `paused`, `failed`, `completed` | backend |
| `completedSteps` | noms des steps terminés, dans l'ordre | backend |
| `skip` | `true` : on saute les steps terminés ; `false` : on repart de zéro | consigne |
| `from` | nom du step où (re)démarrer | consigne |
| `checkpointDir` | dossier des résultats intermédiaires | configuration |

`skip` et `from` sont des **consignes** : elles sont lues au démarrage puis remises à leur valeur par défaut dans le fichier
réécrit. C'est ce qui garantit que relancer `output/run.json` reprend là où on en est.

## 8. Dépannage

| Message | Cause et solution |
|---|---|
| `not a directory: images/` | le dossier `import.path` n'existe pas **depuis le dossier courant**. Lancer depuis la racine du projet ou donner un chemin absolu. |
| `no file matching '...' in ...` | aucun fichier ne correspond à `import.format`. Vérifier le nommage (section 4). |
| `format '...' needs 2 groups` | la regex d'import doit avoir deux groupes capturants : colonne, ligne. |
| `invalid format '...'` | la regex d'import est invalide. |
| `step 'x': unknown option 'y'` | option inconnue dans le script. Régénérer le script avec `generate_pipeline`. |
| `'step.option': step '...' is not in the pipeline` | option donnée pour un step exclu par `--steps`. |
| `cannot start at 'x': step N must be completed first` | `--from` dépasse le premier step non terminé. |
| `... is not a valid int/float/bool for option` | valeur du mauvais type dans `generate_pipeline`. |
| `warning: checkpoint incomplete (no serializer for type ...)` | un step produit un type de donnée sans sérialiseur : il sera refait à la reprise (section 10). |
| un step « réussit » sans rien produire | normal pour l'instant : seuls `import` et le moteur sont implémentés. |

## 9. État actuel et limites

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

# Documentation technique

## 10. Architecture

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

### Déroulement de `Pipeline::run()`

1. **Point de départ** : nombre de steps de tête dont le nom concorde avec `completedSteps` **et** dont le checkpoint est
   valide (complet, mêmes options). `from` permet de revenir plus tôt, jamais plus loin. `skip=false` force le départ à 0.
2. **Nettoyage** : les checkpoints des steps à refaire sont supprimés.
3. **Restauration** du `DataStore` : pour chaque clé, la dernière version écrite par un step déjà terminé.
4. **Exécution** des steps restants. Après chaque step réussi : sauvegarde de ses sorties **puis** ajout à
   `completedSteps` **puis** réécriture du script (toujours dans cet ordre). Un step qui échoue ou lève une exception
   donne `status=failed`. Une demande de pause (`requestPause()`, thread-safe) arrête proprement entre deux steps.

### Checkpoints sur disque

```text
output/checkpoints/
  00_import/manifest.json    ← écrit EN DERNIER : sa présence = checkpoint complet
  00_import/tiles.json, tiles_0000.tif, tiles_0001.tif, ...
  01_stitching/...
```

Un dossier **par step** (et non par clé de données) : `intensityCorrection` consomme et produit `tiles` ; avec un dossier par
step, reprendre avant lui redonne les tuiles d'origine. Toutes les écritures de fichiers d'état sont atomiques
(fichier temporaire puis `rename`).

## 11. Ajouter ou compléter un step

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

## 12. Historique

Le champ `run`, les checkpoints, la reprise et le générateur `generate_pipeline` ont été ajoutés après la mise en place du
parseur JSON et des squelettes de steps (branche `fe/pipeline-json-parser`).
