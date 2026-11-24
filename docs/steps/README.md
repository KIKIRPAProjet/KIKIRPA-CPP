# Les steps

Une fiche par step. Chacune commence par **Entrées et sorties** : les clés que le step lit et écrit dans le DataStore, leur type, le step qui les fournit et ceux qui les utilisent. Elle précise aussi ce que le step lit de chaque entrée et ce qu'il écrit dans chaque sortie.

| Step (`name`) | Lit | Écrit | Fiche |
|---|---|---|---|
| `import` | les images du dossier `path` | `tiles` | [ImportStep.md](ImportStep.md) |
| `overlap-estimation` | `tiles` | `overlapEstimates` | [OverlapEstimationStep.md](OverlapEstimationStep.md) |
| `phase-correlation` | `tiles` | `matches` | [PhaseCorrelationStep.md](PhaseCorrelationStep.md) |
| `sift` | `tiles` | `matches` | [SiftStep.md](SiftStep.md) |
| `lightglue` | `tiles`, le modèle ONNX `modelPath` | `matches` | [LightGlueStep.md](LightGlueStep.md) |
| `vote-2d` | `matches` | `matches` | [Vote2DStep.md](Vote2DStep.md) |
| `manual-matches` | `tiles`, `matches`, les corrections `corrections` | `matches` | [ManualMatchesStep.md](ManualMatchesStep.md) |
| `absolute-pos` | `tiles`, `matches` | `positions` | [AbsolutePosStep.md](AbsolutePosStep.md) |
| `intensity-correction` | `tiles`, `matches` | `intensityCorrection` | [IntensityCorrectionStep.md](IntensityCorrectionStep.md) |
| `graphcut` | `tiles`, `positions` | `territories` | [GraphCutStep.md](GraphCutStep.md) |
| `final-verification` | chemins TIFF/JPEG configurés | `verificationReport` | [FinalVerificationStep.md](FinalVerificationStep.md) |
| `dynamic-programming-dijkstra` | `tiles`, `positions`, `intensityCorrection` | `seamlines` | [dp-dijkstrat.md](dp-dijkstrat.md) |
| `fusion` | `tiles` (TIFF pyramidaux, région par région), `positions`, `territories`, `intensityCorrection` si présente | `mosaic` (trois TIFF écrits bloc par bloc) | [FusionStep.md](FusionStep.md) |
| `export` | `mosaic` | `mosaic.tif` ou `mosaic.jpg` dans `outputDirectory` | [ExportStep.md](ExportStep.md) |

`overlap-estimation` mesure le recouvrement réel de chaque paire : c'est la première passe commune aux trois méthodes d'appariement. Placé avant elles, il leur donne leur point de départ ; sans lui, chacune la fait elle-même. `phase-correlation`, `sift` et `lightglue` sont trois façons de produire `matches` : un pipeline en utilise une seule. `graphcut` et `dynamic-programming-dijkstra` sont deux méthodes de couture.

- Le schéma de l'enchaînement, et le JSON de chaque type de donnée : [COMPRENDRE_LE_PROJET.md §4](../COMPRENDRE_LE_PROJET.md#4-ce-que-chaque-step-reçoit-et-produit).
- **Ajouter un step**, avec un exemple complet (code, test, enregistrement, fiche) : [AJOUTER_UN_STEP.md](AJOUTER_UN_STEP.md).
- Le script JSON, les contrôles de `buildPipeline` et les checkpoints : [core/Pipeline.md](../core/Pipeline.md).
