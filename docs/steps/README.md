# Les steps

Une fiche par step. Chacune commence par **Entrées et sorties** : les clés que le step lit et écrit dans le DataStore, leur type, le step qui les fournit et ceux qui les utilisent. Elle précise aussi ce que le step lit de chaque entrée et ce qu'il écrit dans chaque sortie.

| Step (`name`) | Lit | Écrit | Fiche |
|---|---|---|---|
| `import` | les images du dossier `path` | `tiles` | [ImportStep.md](ImportStep.md) |
| `phase-correlation` | `tiles` | `matches` | [PhaseCorrelationStep.md](PhaseCorrelationStep.md) |
| `sift` | `tiles` | `matches` | [SiftStep.md](SiftStep.md) |
| `lightglue` | `tiles`, le modèle ONNX `modelPath` | `matches` | [LightGlueStep.md](LightGlueStep.md) |
| `absolute-pos` | `tiles`, `matches` | `positions` | [AbsolutePosStep.md](AbsolutePosStep.md) |
| `intensity-correction` | `tiles`, `matches` | `intensityCorrection` | [IntensityCorrectionStep.md](IntensityCorrectionStep.md) |
| `graphcut` | `tiles`, `positions` | `territories` | [GraphCutStep.md](GraphCutStep.md) |
| `dynamic-programming-dijkstra` | `tiles`, `positions`, `intensityCorrection` | `seamlines` | [dp-dijkstrat.md](dp-dijkstrat.md) |
| `mosaic-crop` | `mosaic` | `mosaic` rognée | [MosaicCropStep.md](MosaicCropStep.md) |

`phase-correlation`, `sift` et `lightglue` sont trois façons de produire `matches` : un pipeline en utilise une seule. `graphcut` et `dynamic-programming-dijkstra` sont deux méthodes de couture.

- Le schéma de l'enchaînement, et le JSON de chaque type de donnée : [COMPRENDRE_LE_PROJET.md §4](../COMPRENDRE_LE_PROJET.md#4-ce-que-chaque-step-reçoit-et-produit).
- **Ajouter un step**, avec un exemple complet (code, test, enregistrement, fiche) : [AJOUTER_UN_STEP.md](AJOUTER_UN_STEP.md).
- Le script JSON, les contrôles de `buildPipeline` et les checkpoints : [core/Pipeline.md](../core/Pipeline.md).
