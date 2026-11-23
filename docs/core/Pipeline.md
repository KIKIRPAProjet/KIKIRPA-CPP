# Pipeline : script JSON, registre des steps, exécution

Code : `core/pipeline/` (`PipelineClass`, `buildPipeline`, `RunState`), `core/step/` (`StepClass`, `DataStoreClass`), `core/config/pipeline_config.h` (format du script).
Test : `tests/core/test_pipeline.cpp` (`ctest -L unit`).

## Lancer un pipeline

La GUI génère le script backend à partir du projet et lance le pipeline réel de façon asynchrone. Chaque exécution reçoit son propre dossier sous `<dossier-de-sortie>/runs/<identifiant>/`, avec son archive `execution.json` et un petit `execution-summary.json` utilisé pour suivre la progression et remplir rapidement la liste d'export. Le résumé est mis à jour pendant le run ; l'archive complète est écrite une seule fois à la fin (succès, échec ou pause). Les exécutions encore en cours ne sont pas proposées à l'export. Le script d'état et les checkpoints restent dans le dossier défini par `run.checkpointDirectory`.

### En ligne de commande

```sh
build/<preset>/stitching --pipeline pipeline.json            # exécute tous les steps
build/<preset>/stitching --pipeline pipeline.json --resume   # reprend le run précédent
build/<preset>/stitching --help
```

Ajouter `.exe` sous Windows. Sans argument (ou avec des options de Qt comme `-platform`), `stitching` ouvre l'interface graphique. Une option longue inconnue (`--pipline`…) est refusée avec le code 2 : elle n'ouvre pas la fenêtre.

- **Chemins relatifs** : les chemins écrits dans le script (`path` d'`import`, `outputDirectory`, `run.checkpointDirectory`…) se lisent **depuis le dossier du script**, quel que soit le dossier d'où l'on lance la commande. Seul le chemin du script lui-même se lit depuis le dossier courant.
- **Caractères non ASCII** : dossiers, noms de tuiles et sorties peuvent contenir n'importe quel caractère (« œuvre_été », « 作品 »). Le script est lu en UTF-8, les arguments de la commande aussi (relus en UTF-16 sous Windows), et les messages sortent en UTF-8 (la console Windows est passée en UTF-8 le temps du run). Règle de code : [CONVENTIONS.md §8](../CONVENTIONS.md#8-structure-du-code), « Chemins ».
- **Messages** : la progression (`Step 3/7 OK {...}`) va sur la sortie standard. Un échec est résumé sur la sortie d'erreur, avec le numéro du step, son nom et sa cause.
- **Codes de sortie** :

| Code | Signification |
|---|---|
| 0 | tous les steps ont réussi |
| 1 | un step a échoué : le run s'est arrêté, les steps précédents restent faits |
| 2 | ligne de commande ou script invalide (step ou paramètre inconnu, entrée manquante, JSON mal formé) : aucun step n'a tourné |
| 3 | erreur de fichier : script introuvable ou illisible, état de reprise illisible |

**Reprise.** Elle demande un `run.checkpointDirectory` dans le script. Pendant le run, l'état (`completedSteps`) est réécrit après chaque step dans `<checkpointDirectory>/run.json` ; **le script de l'utilisateur n'est jamais modifié**.
- Avec `--resume`, les steps déjà faits dont le checkpoint est encore valide (mêmes paramètres, mêmes entrées) sont sautés, et le run reprend au premier step à refaire. On peut donc corriger le paramètre d'un step qui a échoué, puis reprendre : seuls ce step et les suivants sont refaits.
- Sans `--resume`, tous les steps sont refaits et les anciens checkpoints sont remplacés.
- `run.startFromStep` n'a d'effet qu'avec `--resume`.
- `fusion` sans `outputDirectory` est refait à chaque reprise (voir [FusionStep.md](../steps/FusionStep.md#checkpoint)).

### Depuis le code

```cpp
PipelineNs::PipelineClass pipeline = PipelineNs::buildPipelineFromFile("pipeline.json");   // ou buildPipeline(json)
bool ok = pipeline.run([](const std::string& message) { std::cout << message << '\n'; });
const auto& tiles = pipeline.getDataStore().get<DataNs::TileList>("tiles");   // sorties des steps
```

Exemple complet et formats des sorties : [COMPRENDRE_LE_PROJET.md](../COMPRENDRE_LE_PROJET.md). Reprise depuis le code : [plus bas](#reprise-pause-checkpoints).

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
| `json` | tableau ou objet JSON ; son contenu est vérifié par le step (ex. la liste `tiles` de `import`) |

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

### Rapport d'exécution

L'appelant active l'archive avec `setReportPath()` et fournit le nom du projet ainsi que l'identifiant d'exécution avec `setReportMetadata()`. Le résumé de progression est écrit de façon atomique après chaque transition ; l'archive complète est écrite de façon atomique à la fin. Le pipeline ne crée pas automatiquement le rapport HTML. Un chemin de rapport vide désactive l'archivage. Une erreur d'écriture de l'archive produit un avertissement et ne bloque pas le calcul. Si seul le résumé ne peut pas être écrit, l'archive finale reste disponible et l'interface peut lire ses métadonnées en solution de repli.

L'archive décrit les étapes dans leur ordre configuré. Elle consigne le statut global, les dates de début et de fin, la configuration, les clés et types des entrées, les sorties résumées et le résultat de chaque étape. Un arrêt laisse les étapes non exécutées au statut `notReached`; celles déjà réutilisées depuis un checkpoint valide portent `skipped`, et une étape en cours d'exécution porte `running`. Pour une étape sautée, le rapport conserve son statut et ses clés de sortie sans recharger une seconde fois les données du checkpoint. Une étape en échec contient le motif disponible. Les tuiles sont documentées par leurs noms, chemins, dimensions et voisinages, sans recopier les pixels. Les correspondances, territoires et coutures sont résumés (comptages, mesures et extrémités utiles) afin de limiter les répétitions et la taille des rapports. Un type non pris en charge est signalé dans le rapport sans faire échouer le pipeline.

Depuis le menu d'export Qt, l'opérateur sélectionne une exécution archivée et exporte son rapport HTML. Cette opération lit uniquement le JSON déjà enregistré : elle ne relance aucun calcul. Pour les anciennes archives sans résumé, l'interface en crée un lors du premier affichage ; les affichages suivants ne lisent que ce petit fichier. Le rapport détaille aussi les dimensions et le chemin de sortie lorsqu'un step `export` a effectivement produit un fichier.

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

**Les consignes, écrites par l'appelant (la GUI, la ligne de commande), et remises à leur défaut au démarrage :**
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
| `Tiles` | les fiches `TileClass` (`id`, `source`, `row`, `column`, `tiff`, `width`, `height`, `type`, voisines). **Les pixels ne sont pas recopiés** : à la reprise, le TIFF pyramidal doit toujours exister. |
| `PairMatches` | `tileIdA`, `tileIdB`, `direction`, `pointsInA`, `pointsInB`, `confidenceScores`, `translationX`, `translationY` |
| `Positions` | `tileId` (l'`id` de la tuile), `position`, `rotationRadians` |
| `Territories` | `tileIndex`, `polygons`, `holes` |
| `IntensityCorrection` | `tileGains`, `fieldStrength`, `isolatedTileIndices` (enregistré par `registerType`, voir `core/checkpoint/intensity_correction_checkpoint.cpp`) |
| `VerificationReport` | rapport JSON de dimensions et métadonnées (enregistré par `registerType`, voir `core/checkpoint/final_verification_checkpoint.cpp`) |
| `PairResiduals` | `tileIdA`, `tileIdB`, `residual`, `residualNorm` (enregistré par `registerType`, dans `steps/absolute_pos_step.cpp`) |
| `Seamlines` | une entrée par couture : `tileIdA`, `tileIdB`, `direction`, `points` |
| `Mosaic` | `imagePath`, `provenancePath`, `maskPath`, `canvasSize`. **Les pixels ne sont pas recopiés** : ils restent dans les TIFF écrits par l'assemblage. |

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
| `absolute-pos` | `StepsNs::AbsolutePosStepClass` | `tiles` : `Tiles`, `matches` : `PairMatches` | `positions` : `Positions`, `pairResiduals` : `PairResiduals` |
| `graphcut` | `StepsNs::GraphCutStepClass` | `tiles` : `Tiles`, `positions` : `Positions` | `territories` : `Territories` |
| `intensity-correction` | `StepsNs::IntensityCorrectionStepClass` | `tiles` : `Tiles`, `matches` : `PairMatches` | `intensityCorrection` : `IntensityCorrection` |
| `final-verification` | `StepsNs::FinalVerificationStepClass` | — (chemins configurés) | `verificationReport` : `VerificationReport` |
| `dynamic-programming-dijkstra` | `StepsNs::DynamicProgrammingDijkstraStepClass` | `tiles` : `Tiles`, `positions` : `Positions`, `intensityCorrection` : `IntensityCorrection` | `seamlines` : `Seamlines` |
| `fusion` | `StepsNs::FusionStepClass` | `tiles` : `Tiles`, `positions` : `Positions`, `territories` : `Territories` (+ `intensityCorrection` lue si présente) | `mosaic` : `Mosaic` |
| `export` | `StepsNs::ExportStepClass` | `mosaic` : `Mosaic` | — (publie `mosaic.tif`) |

`phase-correlation`, `sift` et `lightglue` sont trois façons de produire `matches` : un pipeline en utilise une seule. `graphcut` et `dynamic-programming-dijkstra` sont deux méthodes de couture. Une fiche par step dans [docs/steps/](../steps/).
`fusion` écrit la mosaïque sur disque, bloc par bloc ; `export`, terminal, la publie. `fusion` lit `intensityCorrection` sans la déclarer : le moteur n'a pas d'entrée facultative.

## Ajouter un step

Marche à suivre complète, avec un exemple compilé et testé (en-tête, constructeur, `execute()`, registre, nouveau type et checkpoint, test GoogleTest, fiche) : [steps/AJOUTER_UN_STEP.md](../steps/AJOUTER_UN_STEP.md).

En bref : une classe qui hérite de `StepNs::StepClass`, dont le constructeur par défaut fixe `name`, `parameters`, `inputs` et `outputs` ; une ligne dans `stepRegistry()` ; `REGISTERED_STEP_COUNT` + 1 dans `tests/core/test_pipeline.cpp`. **Le type C++ derrière une clé est le même pour tous les steps**, sinon `get<T>` lève `std::bad_any_cast` à l'exécution.
