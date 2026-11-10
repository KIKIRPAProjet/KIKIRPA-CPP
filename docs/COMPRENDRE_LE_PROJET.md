# Comprendre le projet KIKIRPA-CPP

Ce guide est **la page à lire en premier**. Il explique comment les pièces du projet s'assemblent : ce que fait le logiciel, comment on le fait tourner, ce que chaque étape reçoit et produit, ce qui marche et ce qui manque. Il renvoie aux pages détaillées pour le reste.

| Pour… | Lire |
|---|---|
| comprendre le projet dans son ensemble | **ce guide** |
| compiler | [BUILD.md](BUILD.md) |
| trouver un fichier dans le code | [CARTE_ARCHITECTURE.md](CARTE_ARCHITECTURE.md) |
| écrire du code et ouvrir une PR | [CONVENTIONS.md](CONVENTIONS.md) |
| le format du script et les checkpoints en détail | [core/Pipeline.md](core/Pipeline.md) |
| un step précis : ses entrées, ses sorties, ses paramètres | [steps/](steps/) |
| ajouter un step | [steps/AJOUTER_UN_STEP.md](steps/AJOUTER_UN_STEP.md) |
| un algorithme précis | [algorithms/](algorithms/) |
| retrouver un ancien nom de fichier ou de classe | [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md) |

> L'état décrit ici est celui de `main` au **09/11/2026**. La branche `fe/export` ajoute un step d'export, en attente de PR. Pour savoir qui travaille sur quoi, voir les issues et les pull requests sur GitHub.

---

## 1. Le projet en une page

Une œuvre est photographiée en **tuiles** : des images qui se recouvrent, rangées en grille (ligne, colonne). Le logiciel doit en faire **une seule image**, sans décalage, sans différence de luminosité et sans couture visible.

Le travail est découpé en **étapes**, exécutées dans l'ordre :

| # | Étape | Question à laquelle elle répond | Step sur main |
|---|---|---|---|
| 1 | Import | Quelles tuiles, où dans la grille ? | ✅ `import` |
| 2 | Recouvrement | De combien les tuiles se chevauchent-elles vraiment ? | ⚙️ intégré aux steps d'appariement ; step dédié dans la PR #90 |
| 3 | Appariement | De combien B est-elle décalée par rapport à A, pour chaque paire de voisines ? | ✅ `phase-correlation`, `sift`, `lightglue` (un seul au choix) |
| 4 | Rejet des aberrants | Quels points appariés sont faux ? | ❌ algorithme prêt (vote 2D), **pas branché** |
| 5 | Positionnement global | Où placer chaque tuile pour respecter au mieux tous les décalages ? | ✅ `absolute-pos` |
| 6 | Intensité | Quelle tuile est plus claire ou plus sombre que ses voisines ? | ✅ `intensity-correction` (calcule les gains, ne les applique pas) |
| 7 | Seamline | Où passe la couture entre deux tuiles ? | ✅ `graphcut` ou `dynamic-programming-dijkstra` |
| 8 | Assemblage | Fabriquer l'image finale | ❌ PR #69 en cours |
| 9 | Export | Écrire le fichier final | 🟡 branche `fe/export`, en attente de PR |

**Sur `main`, le pipeline calcule tout ce qu'il faut pour fabriquer la mosaïque (positions, gains, coutures), mais ne la fabrique pas encore.** La branche `fe/export` ajoute l'écriture TIFF, qui attend les blocs produits par Fusion.

---

## 2. Comment le code est organisé, et pourquoi

### Trois couches

```
 frontend/ (Qt)            interface : aujourd'hui des maquettes, non reliées au backend (§9)
      │ écrit un script JSON, lance le pipeline
      ▼
 core/                     le moteur : lit le script, enchaîne les steps, sauve les checkpoints
      │ appelle execute() sur chaque step
      ▼
 steps/                    les adaptateurs : un step lit ses entrées dans le DataStore,
      │                    appelle un algorithme, range ses sorties dans le DataStore
      ▼
 algorithms/               le calcul pur : ne connaît ni le pipeline, ni le JSON du script
```

- **Un algorithme** (`algorithms/…`) reçoit des images ou des structures C++ et renvoie un résultat. Il peut être testé seul.
- **Un step** (`steps/…`, une classe qui hérite de `StepNs::StepClass`) fait le lien entre le pipeline et un algorithme. Il déclare son nom, ses paramètres, ses entrées et ses sorties.
- **Le moteur** (`core/…`) ne connaît aucun algorithme. Il exécute des steps.

### Les quatre notions du moteur

| Notion | Ce que c'est | Code |
|---|---|---|
| **Script** | le fichier JSON qui dit quels steps exécuter, dans quel ordre, avec quels paramètres | `core/config/pipeline_config.h` |
| **Registre** | la table « nom JSON → classe C++ » : `"sift"` → `SiftStepClass` | `stepRegistry()` dans `pipeline_builder.cpp` |
| **DataStore** | un casier partagé par les steps, indexé par clé (`"tiles"`, `"matches"`…). Un step y lit ses entrées et y range ses sorties | `core/step/data_store.h` |
| **Checkpoint** | la copie sur disque des sorties de chaque step, pour reprendre un run sans tout refaire | `core/checkpoint/` |

### Pourquoi cette architecture

- **Interchangeabilité** : `phase-correlation`, `sift` et `lightglue` produisent la même sortie (`matches`, de type `PairMatches`). On change de méthode en changeant une ligne du script, sans toucher au code des étapes suivantes.
- **Vérification avant de lancer** : chaque step déclare ses entrées et ses sorties. `buildPipeline` refuse un script où une entrée n'est produite par aucun step précédent (par exemple `graphcut` sans `absolute-pos`), avant d'avoir lu une seule image.
- **Reprise** : sur de grandes œuvres, un run peut durer longtemps. Les checkpoints permettent de reprendre après un arrêt, ou de refaire seulement les steps dont on a changé les paramètres.
- **Une interface qui pilote sans savoir calculer** : le front n'a qu'à écrire un script JSON et lancer le pipeline. Il n'appelle aucun algorithme directement.
- **Ne jamais charger toute l'œuvre en mémoire** : voir §6.

---

## 3. Faire tourner le pipeline aujourd'hui

### Il n'y a pas encore d'exécutable

`src/main.cpp` (l'exécutable `stitching`) ne contient qu'un `TODO`, et la GUI n'appelle pas le backend. Aujourd'hui, le pipeline se lance seulement depuis du code C++ : un test, ou un `main` écrit pour l'occasion. Écrire le vrai exécutable en ligne de commande est une tâche à faire.

Pour essayer sur ses propres images, remplacer **localement** le contenu de `src/main.cpp` par ceci, sans le committer :

```cpp
#include "core/pipeline/pipeline_builder.h"

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: stitching <script.json>\n"; return 2; }
    try {
        PipelineNs::PipelineClass pipeline = PipelineNs::buildPipelineFromFile(argv[1]);

        PipelineNs::RunState runState = pipeline.getRunState();
        runState.checkpointDirectory = "output/checkpoints";   // les sorties de chaque step y sont écrites
        pipeline.setRunState(runState);
        pipeline.setScriptPath("output/checkpoints/run.json");

        const bool ok = pipeline.run([](const std::string& message) { std::cout << message << '\n'; });
        return ok ? 0 : 1;
    } catch (const std::exception& exception) {   // script invalide : step inconnu, entrée manquante...
        std::cerr << exception.what() << '\n';
        return 2;
    }
}
```

Puis compiler la cible `stitching` et la lancer avec le script ci-dessous (`build/<preset>/stitching pipeline.json`).

### Un script qui va jusqu'au bout de ce qui existe

```json
{
  "pipeline": {
    "steps": [
      { "name": "import",
        "parameters": [
          { "valueType": "string", "name": "path", "value": "D:/oeuvre/tuiles" },
          { "valueType": "string", "name": "format", "value": ".*_r(?=\\d+_c(\\d+)\\.tiff?$)(\\d+)_c\\d+\\.tiff?" }
        ] },
      { "name": "phase-correlation",
        "parameters": [ { "valueType": "int", "name": "niveau", "value": 2 } ] },
      { "name": "absolute-pos" },
      { "name": "intensity-correction" },
      { "name": "graphcut",
        "parameters": [ { "valueType": "float", "name": "scale", "value": 0.25 } ] }
    ]
  }
}
```

- Un paramètre absent garde sa valeur par défaut. Les paramètres de chaque step sont dans sa fiche ([steps/](steps/)).
- **Piège n° 1 : le nom des fichiers.** `absolute-pos` et `graphcut` exigent des noms qui finissent par `_r<ligne>_c<colonne>.tif`, alors que le format par défaut d'`import` attend `img_C<colonne>_R<ligne>.tif`. D'où le `format` ci-dessus. Explication : [steps/ImportStep.md](steps/ImportStep.md).
- **Piège n° 2 : un seul step d'appariement.** `phase-correlation`, `sift` et `lightglue` écrivent tous dans `matches`. Si un script en contient deux, `buildPipeline` l'accepte, mais le second écrase le résultat du premier.
- `lightglue` demande un modèle ONNX (`modelPath`) : [steps/LightGlueStep.md](steps/LightGlueStep.md).

### Ce que l'on voit dans la console

Une ligne par step, avec son compte rendu (`getResult()`) :

```
Step 1/5 OK {"tiles":6}
Step 2/5 OK {"confidence":[...],"failures":[],"niveau":2,"pairs":7}
Step 3/5 OK {"failures":[],"pairs":7,"positions":6}
...
```

En cas d'échec : `Step 3/5 failed: <raison>`, et le run s'arrête.

### Ce que l'on trouve sur le disque

```
D:/oeuvre/tuiles/.cache/pyramids/      un TIFF pyramidal par image, écrit par import
output/checkpoints/
├── run.json                           le script complet + l'état du run, réécrit après chaque step
├── 00_import/
│   ├── tiles.json                     la sortie du step
│   └── manifest.json                  écrit en dernier : sa présence marque le checkpoint complet
├── 01_phase-correlation/  matches.json, manifest.json
├── 02_absolute-pos/       positions.json, manifest.json
├── 03_intensity-correction/  intensityCorrection.json, manifest.json
└── 04_graphcut/           territories.json, manifest.json
```

Relancer le même script reprend où le run s'était arrêté, et saute les steps dont le checkpoint est encore valide. Détails : [core/Pipeline.md](core/Pipeline.md#reprise-pause-checkpoints).

---

## 4. Ce que chaque step reçoit et produit

### Vue d'ensemble

```
                 ┌─────────┐
  images .tif ──►│ import  │──► tiles ─────────────┬──────────────────┬───────────────────┐
                 └─────────┘                       │                  │                   │
                                                   ▼                  │                   │
                    ┌───────────────────────────────────────┐         │                   │
                    │ phase-correlation  OU  sift  OU  lightglue │    │                   │
                    └───────────────────────────────────────┘         │                   │
                                                   │ matches          │                   │
                              ┌────────────────────┼──────────────────┘                   │
                              ▼                    ▼                                      │
                     ┌──────────────┐    ┌──────────────────────┐                         │
                     │ absolute-pos │    │ intensity-correction │──► intensityCorrection  │
                     └──────────────┘    └──────────────────────┘                         │
                              │ positions                                                 │
                              ▼                                                           │
                         ┌──────────┐◄───────────────────────────────────────────────────┘
                         │ graphcut │──► territories
                         └──────────┘
                                         (assemblage et export : pas encore sur main)
```

Non dessiné : `dynamic-programming-dijkstra` lit `tiles`, `positions` et `intensityCorrection`, et produit `seamlines`. C'est l'autre méthode de couture, à côté de `graphcut`.

| Step | Paramètres principaux | Entrées | Sortie | Fiche |
|---|---|---|---|---|
| `import` | `path`, `format` | — | `tiles` : `Tiles` | [ImportStep.md](steps/ImportStep.md) |
| `phase-correlation` | `overlap`, `niveau` (0–3), réglages de la corrélation | `tiles` | `matches` : `PairMatches` | [PhaseCorrelationStep.md](steps/PhaseCorrelationStep.md) |
| `sift` | `overlap`, `maxFeatures`, `loweRatio`, `minMatches`… | `tiles` | `matches` : `PairMatches` | [SiftStep.md](steps/SiftStep.md) |
| `lightglue` | `modelPath`, `minMatches`, `overlap` | `tiles` | `matches` : `PairMatches` | [LightGlueStep.md](steps/LightGlueStep.md) |
| `absolute-pos` | `referenceTileId` | `tiles`, `matches` | `positions` : `Positions`, `pairResiduals` : `PairResiduals` | [AbsolutePosStep.md](steps/AbsolutePosStep.md) |
| `intensity-correction` | `method`, seuils, itérations | `tiles`, `matches` | `intensityCorrection` : `IntensityCorrection` | [IntensityCorrectionStep.md](steps/IntensityCorrectionStep.md) |
| `graphcut` | `scale` | `tiles`, `positions` | `territories` : `Territories` | [GraphCutStep.md](steps/GraphCutStep.md) |
| `dynamic-programming-dijkstra` | `method`, `costFunction`, `corridorRadius` | `tiles`, `positions`, `intensityCorrection` | `seamlines` : `Seamlines` | [dp-dijkstrat.md](steps/dp-dijkstrat.md) |
| `export` | `outputDirectory` | `compositeBlocks` : `CompositeBlocks` | aucun DataStore ; écrit `mosaic.tif` | [ExportStep.md](steps/ExportStep.md) |

Chaque step a aussi un compte rendu, `getResult()` : compteurs, `failures` (paires ignorées, avec la raison) et `error` en cas d'échec. Il est affiché dans la console, mais il n'est pas une sortie du pipeline.

### Les types de données, et leur JSON

Dans le DataStore, une donnée est une structure C++. Les types pris en charge par les checkpoints deviennent un fichier `<clé>.json`. `CompositeBlocks` est échangé en mémoire et n'est pas encore sérialisé. Les exemples ci-dessous ont la forme exacte écrite par `core/checkpoint/checkpoint_types.cpp` ; les valeurs sont inventées.

**Conventions communes à tous les types :**
- les coordonnées sont en **pixels de pleine résolution** ;
- x va vers la droite, y vers le bas ;
- un point s'écrit `[x, y]`.

#### `Tiles` (clé `tiles`) : la liste des tuiles

Une fiche par tuile, **sans pixels**. Les pixels restent dans les fichiers. Les voisines sont données par identifiant, `-1` s'il n'y en a pas.

```json
[ { "id": 0, "source": "D:/oeuvre/tuiles/t_r00_c00.tif", "tiff": "D:/oeuvre/tuiles/.cache/pyramids/t_r00_c00_pyramid.tif",
    "width": 9000, "height": 7000, "type": 18, "left": -1, "right": 1, "top": -1, "bottom": 2 } ]
```

C++ : `DataNs::TileList` = `std::vector<std::shared_ptr<DataNs::TileClass>>` (`core/data/tile.h`).

#### `PairMatches` (clé `matches`) : le décalage entre deux voisines

Une entrée par paire de voisines, de A vers sa voisine de droite ou du dessous B.

```json
[ { "tileIdA": 0, "tileIdB": 1, "direction": "right",
    "pointsInA": [ [8100.0, 512.0], [8230.5, 3010.0] ],
    "pointsInB": [ [ 101.2, 530.1], [ 231.6, 3028.0] ],
    "confidenceScores": [0.98, 0.95],
    "translationX": 7998.9, "translationY": -18.1 } ]
```

- `pointsInA[i]` correspond à `pointsInB[i]`.
- `(translationX, translationY)` = position du coin haut-gauche de B dans le repère de A, environ `pointsInA[i] − pointsInB[i]`.
- C'est la translation qu'utilisent `absolute-pos` et `intensity-correction`. Les points servent au contrôle.

C++ : `std::vector<DataNs::PairMatches>` (`core/data/algorithm_types.h`).

#### `Positions` (clé `positions`) : où placer chaque tuile

```json
[ { "tileId": "r00_c00", "position": [0.0, 0.0],     "rotationRadians": 0.0 },
  { "tileId": "r00_c01", "position": [7998.9, -18.1], "rotationRadians": 0.0 } ]
```

- `position` = coin haut-gauche de la tuile dans le repère de la mosaïque.
- La tuile de référence est en (0, 0). Les autres peuvent avoir des coordonnées négatives.
- `tileId` est lu dans le nom du fichier (`_r00_c01`), **ce n'est pas l'`id` numérique**.
- La rotation vaut toujours 0 : `absolute-pos` ne calcule que des translations.

C++ : `std::vector<DataNs::TilePositionData>`.

#### `PairResiduals` (clé `pairResiduals`) : l'écart de chaque paire après ajustement

```json
[ { "tileIdA": 0, "tileIdB": 1, "residual": [-1.16, 3.78], "residualNorm": 3.95 } ]
```

- `residual` = translation mesurée − (position B − position A) ajustées, en pixels.
- Un grand résidu indique une mesure en contradiction avec les autres. Sans boucle dans la grille, il vaut 0.
- `tileIdA` et `tileIdB` sont les `id` numériques, comme dans `PairMatches`.

C++ : `std::vector<DataNs::PairResidual>`.

#### `IntensityCorrection` (clé `intensityCorrection`) : la luminosité de chaque tuile

```json
{ "tileGains": [1.04, 0.97, 1.0, 0.99], "fieldStrength": -0.12, "isolatedTileIndices": [] }
```

- `tileGains[i]` est la luminosité relative de la i-ème tuile de `tiles`. Pour la corriger, on **divise** ses pixels par le gain, et aussi par `exp(fieldStrength · r²)`, l'assombrissement vers les bords.
- Une tuile listée dans `isolatedTileIndices` ne se corrige pas.

C++ : `DataNs::IntensityCorrection` (`core/data/algorithm_types.h`).

#### `Territories` (clé `territories`) : quelle tuile fournit quels pixels

```json
[ { "tileIndex": 0, "polygons": [ [ [0, 0], [8650, 0], [8641, 6800], [0, 6800] ] ], "holes": [ [] ] } ]
```

- `tileIndex` est la place de la tuile dans `tiles`.
- Les polygones sont en pixels entiers de la mosaïque, dans un repère décalé pour que le coin le plus petit soit en (0, 0).
- Les bords entre territoires **sont** les seamlines.

C++ : `std::vector<DataNs::TileTerritory>`.

#### `Seamlines` (clé `seamlines`) : le tracé de chaque couture

```json
[ { "tileIdA": 0, "tileIdB": 1, "direction": "right", "points": [ [8650.0, 0.0], [8649.0, 1.0], [8647.0, 2.0] ] } ]
```

- Une entrée par paire de voisines, de A vers sa voisine de droite ou du dessous B.
- `points` : le chemin de la couture, pixel par pixel, en pleine résolution et dans le repère de la mosaïque.
- Produit par `dynamic-programming-dijkstra`. `graphcut` ne produit pas de `seamlines` : ses coutures sont les bords des territoires.

C++ : `DataNs::Seamlines` = `std::vector<DataNs::Seamline>` (`core/data/algorithm_types.h`).

#### `CompositeBlocks` (clé `compositeBlocks`) : les blocs d'image assemblés

Chaque bloc contient l'image couleur `imageRgb` (`CV_16UC3`), son rectangle `blockRect` dans la mosaïque, ainsi que `provenance` et `mask`. L'export lit seulement `imageRgb` et `blockRect` et écrit l'image couleur dans `mosaic.tif`. Les blocs doivent former une grille complète alignée sur `DataNs::BLOCK_SIZE` ; les blocs de bord peuvent être plus petits.

C++ : `DataNs::CompositeBlockList` = `std::vector<DataNs::CompositeBlock>` (`core/data/algorithm_types.h`). Ce type n'est pas encore sérialisé dans les checkpoints.

#### Les deux fichiers du moteur

`manifest.json`, un par step, sert à décider si le checkpoint est encore valable :

```json
{ "step": "import",
  "parameters": [ { "valueType": "string", "name": "path", "value": "D:/oeuvre/tuiles" }, … ],
  "inputs": [ ["t_r00_c00.tif", 145000000, 133456789012345678], … ],
  "outputs": [ { "key": "tiles", "dataType": "Tiles" } ],
  "complete": true }
```

`run.json` contient le script complet, plus l'état du run. Le front pourra s'en servir pour afficher l'avancement :

```json
{ "pipeline": { "steps": [ … ] },
  "run": { "status": "running", "completedSteps": ["import", "phase-correlation"],
           "skipCompletedSteps": true, "startFromStep": "", "checkpointDirectory": "output/checkpoints" } }
```

`status` vaut `idle`, `running`, `paused`, `failed` ou `completed`.

---

## 5. Du JSON à l'image reconstruite

Les JSON ci-dessus **décrivent** la mosaïque, ils ne la contiennent pas. Pour obtenir l'image, il faut un step d'assemblage, puis un step d'export. **Aucun des deux n'est encore sur `main`.**

Ce que l'assemblage devra faire, pour chaque tuile, une à la fois :

1. lire ses pixels (fichier source ou TIFF pyramidal) ;
2. corriger sa luminosité : diviser par `tileGains[i] · exp(fieldStrength · r²)`. Ce calcul existe déjà dans `BrightnessEqualizationNs::OutputClass` (`algorithms/brightness_equalization/output.h`) ;
3. la placer à sa `position`, après le même décalage que `graphcut` (coin le plus petit en (0, 0)) ;
4. ne garder que les pixels qui tombent dans son **territoire** ;
5. écrire le résultat dans l'image finale, **par blocs** et sans jamais garder toute la mosaïque en mémoire (§6).

L'export écrit les blocs composites dans un TIFF tuilé `mosaic.tif`.

Où en est-on :

- **Assemblage (Fusion)** : PR #69 (brouillon).
- **Export** : branche `fe/export`, qui ajoute le step `export` et son type partagé.
- **Contrat entre les deux** : Fusion doit fournir la clé `compositeBlocks` (`DataNs::CompositeBlockList`). Chaque bloc doit contenir `imageRgb` en `CV_16UC3` et un `blockRect` aligné sur `DataNs::BLOCK_SIZE`, avec une grille complète. L'export n'écrit pas `provenance` ni `mask`.
- `graphcut` construit déjà en interne un aperçu 8 bits de la mosaïque (`finalMosaic`), sans correction de luminosité, puis le jette. Il pourrait servir d'aperçu rapide en attendant le vrai assemblage.

---

## 6. La mémoire : pourquoi on en parle autant

### C++ n'a pas de ramasse-miettes

Contrairement à Java, Python ou C#, **C++ n'a pas de garbage collector**. En C, on libère à la main avec `free`. En C++ moderne, on ne libère pas non plus à la main, mais pour une autre raison : le **RAII**.

Une ressource (mémoire, fichier, thread) appartient à un objet, et elle est libérée **au moment exact où l'objet est détruit**, en général à la fin du bloc `{ }` où il a été créé.

```cpp
{
    cv::Mat image = cv::imread(chemin);   // les pixels sont alloués ici
    traiter(image);
}                                         // ... et libérés ici, automatiquement
```

- `cv::Mat`, `std::vector` et `std::string` libèrent leur mémoire dans leur destructeur.
- `std::unique_ptr` et `std::shared_ptr` remplacent `new` et `delete`.
- Les règles du projet sont dans [CONVENTIONS.md §11.6](CONVENTIONS.md#116-mémoire).

Le RAII dit **quand** la mémoire est libérée. Il ne dit pas **combien** on en utilise en même temps, et c'est là le vrai problème du projet.

### Le vrai problème : la taille des images

Une tuile en 16 bits couleur occupe 6 octets par pixel : une tuile de 100 millions de pixels pèse donc 600 Mo en mémoire, et une œuvre en compte des dizaines. **On ne peut pas charger toutes les tuiles, ni la mosaïque entière.** D'où les règles de conception :

- **Une fiche de tuile ne contient aucun pixel** (`TileClass`). Les pixels restent sur disque.
- **Une ou deux tuiles en mémoire à la fois**, jamais toutes : `import`, `intensity-correction` et `graphcut` respectent cette règle.
- **Le TIFF pyramidal** : à l'import, chaque image est réécrite en blocs de 512 × 512 pixels, à 4 résolutions (1, 1/2, 1/4, 1/8). `IONs::PyramidalTiffReaderClass::readRegion` permet de lire **un rectangle à un niveau donné**, sans charger le reste ([core/PyramidalTiff.md](core/PyramidalTiff.md)).
- **Travailler en résolution réduite** quand c'est possible : `niveau` de `phase-correlation`, réduction 1/8 de `intensity-correction`, `scale` de `graphcut`.

### À quoi sert `MemoryScopeClass`

`MemoryNs::MemoryScopeClass` (`core/memory/memory_scope.h`) **ne gère pas la mémoire : il la mesure.** Placé dans un bloc de code, il relève la mémoire du processus (avant, pic, après) et l'écrit dans le compte rendu du step. Il sert à vérifier les règles ci-dessus sur de vraies œuvres.

### Ce qui ne respecte pas encore ces règles

- **Aucun step ne lit le TIFF pyramidal.** Tous relisent l'image source entière (`cv::imread`), puis la réduisent si besoin. `readRegion` existe mais n'est pas utilisé.
- `sift` et `lightglue` gardent deux images pleine résolution en mémoire.
- `graphcut` alloue deux images de la taille de la mosaïque entière (1 + 3 octets par pixel).
- `MemoryScopeClass` n'est utilisé par aucun step.

---

## 7. Les algorithmes

| Algorithme | À quoi il sert | Comment il marche, en bref | État | Doc |
|---|---|---|---|---|
| Estimation du recouvrement (`OverlapEstimationNs`) | trouver le vrai recouvrement d'une paire avant l'appariement fin | corrélation de phase grossière sur les bandes communes, d'où un décalage au pixel près | ✅ utilisé par les trois steps d'appariement | [OverlapEstimation.md](algorithms/stitching/overlap_estimation/OverlapEstimation.md) |
| Corrélation de phase (`PhaseCorrelationNs`) | décalage précis entre deux tuiles | dans le domaine de Fourier, un décalage devient un déphasage : le pic de la corrélation donne la translation, affinée au sous-pixel, puis contrôlée sur des petits patchs (NCC) | ✅ step `phase-correlation` | [PhaseCorrelation.md](algorithms/stitching/phase_correlation/PhaseCorrelation.md) |
| SIFT (`SiftNs`) | apparier des points remarquables | détecte des points caractéristiques dans les deux bandes, compare leurs descripteurs, garde les paires nettes (test de Lowe) ; translation = médiane | ✅ step `sift` | fiche du step : [SiftStep.md](steps/SiftStep.md) |
| LightGlue (`LightGlueNs`) | apparier des points par réseau de neurones | un modèle ONNX détecte et apparie les points ; on garde les paires de score ≥ 0,9 ; translation = médiane | ✅ step `lightglue` | fiche du step : [LightGlueStep.md](steps/LightGlueStep.md) |
| Vote 2D (`RejectionNs::VoteRejectorClass`) | éliminer les faux appariements | chaque paire de points vote pour une translation dans une grille ; on garde la case la plus votée, puis on affine par médianes successives | ⚠️ codé et testé, **pas branché** | [Vote2D.md](algorithms/rejection/vote_2d/Vote2D.md) |
| Solveur de positions (`PositioningNs::solveRelativePositions`) | placer toutes les tuiles d'un coup | chaque paire donne une équation `position B − position A = translation` ; moindres carrés sur toutes les équations (SVD) | ✅ step `absolute-pos` | [AbsolutePosStep.md](steps/AbsolutePosStep.md) |
| Égalisation de luminosité (`BrightnessEqualizationNs`) | égaliser la luminosité entre tuiles | compare les pixels des recouvrements sur des images réduites ; moindres carrés robustes pour trouver un gain par tuile et un champ d'éclairage commun | ✅ step `intensity-correction` (estimation seulement) | [BrightnessEqualization.md](algorithms/brightness_equalization/BrightnessEqualization.md) |
| Graph cut (`GraphCutNs`) | couture entre les tuiles | la coupe de graphe d'OpenCV cherche la frontière où les deux tuiles se ressemblent le plus, paire par paire ; on en tire les territoires | ✅ step `graphcut` | [GraphCutStep.md](steps/GraphCutStep.md) |
| DP / Dijkstra (`DynamicProgrammingDijkstraNs`) | couture, autre méthode | carte de coût sur le recouvrement (gradients, différences de couleur), puis chemin de coût minimal : programmation dynamique, Dijkstra, ou DP puis Dijkstra dans un couloir | ✅ step `dynamic-programming-dijkstra` ; algorithme testé dans `tests/seamline/` | [dp-dijkstrat.md](steps/dp-dijkstrat.md) (step) |
| Ancien positionneur (`PositioningNs::computeTilePositions`) | ancienne chaîne complète SIFT → vote → moindres carrés | — | ⚠️ code mort : appelé par aucun step ni aucun test | — |

---

## 8. Compilation et CI

- **Compiler** : [BUILD.md](BUILD.md). Toutes les dépendances sont obligatoires : OpenCV avec `dnn`, libtiff, Qt 6, GoogleTest, nlohmann_json, ONNX Runtime. Les deux dernières sont téléchargées automatiquement par CMake.
- **Cibles** : `kikirpa_backend` (bibliothèque : moteur, steps, algorithmes), `kikirpa_frontend` (bibliothèque Qt), `stitching` (l'exécutable, vide pour l'instant) et les exécutables de tests.
- **Tests** : `ctest --preset <preset>`. Ce sont uniquement des tests GoogleTest, rangés par dossier dans `tests/`.

La CI tourne sur chaque push et chaque PR (`.github/workflows/ci.yml`) :

| Job | Ce qu'il vérifie |
|---|---|
| Build and test | compilation et tests sous Ubuntu 24.04 (paquets `apt`) |
| Build and test (Windows) | compilation et tests sous Windows avec MSVC (preset `windows-aqt`) |
| clang-tidy | règles de style et de qualité, **seulement sur les fichiers C++ modifiés** par la branche |

`release.yml` produit les archives Linux et Windows quand on pousse un tag `v*`. Il n'y a pas encore de devcontainer (GitHub Codespaces).

---

## 9. Le frontend

### Ce qu'il y a sur main

Presque rien : `src/frontend/PipelineJson.cpp`, qui sait **écrire** un script JSON de pipeline (`FrontendNs::pipelineToJson`, `writePipelineJson`). Aucune fenêtre.

### Ce qu'il y a dans les branches

Plusieurs maquettes de l'interface Qt existent en branche (`Valerie`, `frontendJonas`, `frontendMathias`, `fr-rahim` / PR #91…) : page d'accueil, grille du projet, onglets, panneau de journal, aperçu.

**Ce sont des maquettes : aucune n'appelle le backend.**
- L'avancement affiché est simulé par un minuteur (`SimulationRunner`, un `QTimer`).
- Les boutons qui ont l'air d'agir n'agissent pas. Par exemple, **Annuler / Rétablir** dans la branche `Valerie` affichent seulement « Dernière action annulée. » dans la barre d'état. Il n'y a ni historique, ni annulation réelle.

### Comment le brancher au backend

Tout ce qu'il faut existe déjà côté backend :

| Le front veut… | Il utilise |
|---|---|
| construire le pipeline à partir des choix de l'utilisateur | `FrontendNs::pipelineToJson`, puis `PipelineNs::buildPipeline` (les erreurs de configuration arrivent en `std::invalid_argument`, à afficher) |
| connaître les steps et leurs paramètres pour les proposer | `PipelineNs::getRegisteredStepNames()`, puis `createStep(nom)->getConfigJson()` |
| lancer sans geler la fenêtre | `pipeline.run(logger)` dans un thread de travail (`QtConcurrent::run`) |
| afficher le journal | le `logger` reçoit chaque message (`Step 2/5 OK …`). Il est appelé **depuis le thread de travail** : passer par un signal Qt en connexion différée pour mettre à jour l'interface |
| mettre en pause | `pipeline.requestPause()`, sans risque depuis le thread de l'interface. La pause est prise entre deux steps |
| reprendre ou refaire à partir d'un step | `RunState` : `checkpointDirectory`, `startFromStep`, `skipCompletedSteps` |
| afficher l'avancement et l'état sauvegardé | `run.json` (`status`, `completedSteps`), réécrit après chaque step |
| afficher les résultats | **après** la fin de `run()`, `pipeline.getDataStore()`, ou à tout moment les `<clé>.json` des checkpoints. Le DataStore n'est pas protégé contre les accès concurrents : ne pas le lire pendant le run |
| dessiner la vraie seamline | les polygones de `territories` : leurs bords sont les coutures |
| dessiner le placement des tuiles | `positions` et les dimensions de chaque tuile (`tiles`) |

Il reste à décider quelle maquette devient l'interface unique, puis à remplacer `SimulationRunner` par ce branchement.

---

## 10. Ce qui reste à faire ou à chercher

À faire, sur une base connue :

- **Fusion et Export** : faire correspondre les sorties de Fusion au contrat `CompositeBlocks`, puis tester la chaîne de bout en bout sur une mosaïque réelle (§5).
- **Brancher le rejet des aberrants** (vote 2D) entre l'appariement et `absolute-pos`.
- **L'exécutable en ligne de commande** (`src/main.cpp`).
- **Le branchement du front** (§9) et le choix d'une seule interface.
- **Lire le TIFF pyramidal** (`readRegion`) dans les steps, au lieu des images source entières (§6).
- **Unifier le nommage des fichiers** entre `import` et `absolute-pos` / `graphcut`.

À chercher ou à décider :

- **Les rotations** : le pipeline suppose des tuiles seulement translatées.
- **Le calage local** : corriger des déformations à l'intérieur des tuiles, au-delà d'une translation globale.
- **Le format et l'organisation de l'export** : TIFF pyramidal, BigTIFF, profil ICC…
- **Le comportement sur de vraies œuvres** : temps, mémoire mesurée avec `MemoryScopeClass`, qualité des coutures.

---

## 11. Petit lexique

| Terme | Sens dans le projet |
|---|---|
| tuile | une des images de départ |
| recouvrement | la bande commune à deux tuiles voisines |
| appariement | mesure du décalage entre deux voisines |
| translation | décalage `(x, y)` de B par rapport à A, en pixels |
| aberrant (outlier) | point apparié à tort |
| position | coin haut-gauche d'une tuile dans la mosaïque |
| gain | luminosité relative d'une tuile |
| seamline | ligne de couture entre deux tuiles |
| territoire | partie de la mosaïque dont les pixels viennent d'une tuile donnée |
| step | une étape du pipeline (une classe qui hérite de `StepClass`) |
| DataStore | le casier où les steps échangent leurs données |
| checkpoint | les sorties d'un step sauvées sur disque, pour reprendre |
| TIFF pyramidal | une image stockée en blocs, à plusieurs résolutions |
