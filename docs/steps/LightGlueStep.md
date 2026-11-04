# LightGlueStepClass

Le step est enregistré sous `lightglue` dans `core/pipeline/pipeline_builder.cpp`.
Son interface est `steps/lightglue_step.h`, son implémentation `steps/lightglue_step.cpp`,
dans le namespace `StepsNs`.

## Configuration JSON

```json
{
  "pipeline": {
    "steps": [
      {"name": "import", "parameters": [{"valueType": "string", "name": "path", "value": "images/"}]},
      {"name": "lightglue", "parameters": [
        {"valueType": "string", "name": "modelPath", "value": "/chemin/modele.onnx"},
        {"valueType": "int", "name": "minMatches", "value": 4},
        {"valueType": "float", "name": "overlap", "value": 0.2}
      ]}
    ]
  }
}
```

Les valeurs absentes gardent les défauts : `modelPath` vide, `minMatches = 4`, `overlap = 0.2`.
`StepClass::importConfig()` vérifie les noms, types et doublons. Le step vérifie les contraintes
métier : chemin non vide, minimum positif dans la plage `int`, recouvrement fini dans `(0, 1)`.
L'ancien format `options` / `type` / `key` n'est plus accepté ; utiliser `parameters` / `valueType` /
`name`. Les anciennes clés de script `model_path` et `min_matches` sont refusées. Le paramètre
interne `model_path` du matcher est traduit par le step.

## Entrées, bandes et coordonnées

Entrée : `DataNs::TileList` sous `tiles`. Les voisins droits et inférieurs sont traités une seule fois.
Les images sources sont lues en 8/16 bits non signés (1, 3 ou 4 canaux), sans modifier leurs pixels.

Chaque paire passe par `OverlapEstimatorClass::estimate()`, puis `computeCommonAreas()`.
Le matcher reçoit seulement les rectangles communs, pas les tuiles entières.
Les points sont validés dans les bandes, puis les origines respectives des rectangles sont ajoutées
à `pointsInA` et `pointsInB`. Leur différence est ajoutée à `translationX` et `translationY`.
Les coordonnées finales sont en pleine résolution dans le repère de chaque tuile ;
`pointsInA - pointsInB` estime la translation de B dans A.

La limite de 3200 pixels du matcher s'applique aux bandes transmises. Une bande dont le grand côté
reste supérieur à cette limite est encore réduite, avec deux facteurs d'échelle pour restaurer les points.
Le step accepte les scores finis dans `[0, 1]`. Le seuil de sélection reste la responsabilité du matcher.
Aucun RANSAC, positionnement global ni assemblage n'est effectué.

## Sortie et erreurs

Le DataStore reçoit `vector<DataNs::PairMatches>` sous `matches`.
`getOutput()` retourne `LightGlueOutput`, avec `key`, `value` et `type`. Sa sérialisation explicite
contient les points, scores, identifiants et translations (`tileIdA`, `tileIdB`, `direction`, `matches`,
`pointsInA`, `pointsInB`, `confidenceScores`, `translationX`, `translationY`).

`getResult()` remplace l'ancien `exportData()` et contient seulement le résumé destiné au journal :

```json
{"pairs": 3, "failures": [{"idA": 0, "idB": 2, "reason": "overlap estimation failed"}]}
```

Une paire en échec est ignorée et les autres sont traitées. Si aucune paire ne réussit, le step
renvoie `false` et ajoute `error`. Une erreur globale (configuration, graphe, chargement du modèle)
arrête le step. La sortie précédente est remplacée par une liste vide au début de chaque exécution.
Le modèle est réutilisé tant que `modelPath` reste inchangé. Un objet step n'est pas partagé entre
plusieurs exécutions simultanées.

## Code commun avec SIFT

`steps/matching_step_utils.h` et `.cpp` sont identiques aux fichiers de la branche SIFT et appartiennent
à `StepsNs`. Ils fournissent `collectPairs`, `readImage`, `validateMatches`, `restoreCoordinates` et
`serializeOutput`. Les steps délèguent ces opérations au même module, sans variante privée.
La fusion des deux branches doit garder une seule copie de ces fichiers et les deux entrées du registre.

## Compilation et tests

ONNX Runtime et le modèle de test sont téléchargés par CMake à la configuration (voir [BUILD.md](../BUILD.md)).
La fabrique de matcher permet de tester le step sans remplacer l'estimation réelle du recouvrement.

Les tests GoogleTest du step vérifient le découpage horizontal/vertical en 8/16 bits, le recalage,
les scores sous 0.9, la sérialisation, les succès partiels, les erreurs d'images, les relances,
les paramètres, le registre JSON et l'échec de chargement du vrai matcher.

Le test `lightglue_model` (label `integration`) tourne avec un vrai modèle, en local comme en CI :

```sh
ctest --test-dir <build> -R '^lightglue_model$' --output-on-failure
```

Il utilise une translation connue et vérifie les correspondances 8/16 bits.

## Modèle vérifié

| | |
|---|---|
| Modèle | `raco_aliked_lightglue_pipeline_k1024.onnx` |
| Provenance | [fabio-sim/LightGlue-ONNX](https://github.com/fabio-sim/LightGlue-ONNX/releases/tag/v3.0), version v3.0 |
| ONNX Runtime | 1.20.1, CPU, Windows x64 |

Le modèle n'est pas versionné dans le dépôt. CMake le télécharge pour les tests ; pour exécuter le step, donner son chemin dans `modelPath`.
`superpoint_lightglue_pipeline.onnx` (v2.0) est refusé avec le message `unexpected RACO/ALIKED output types`.

Sur 4 tuiles réelles de 5176×5176 pixels (16 bits), comparaison avec la corrélation de phase :

| Paire | Corrélation de phase | LightGlue | Points | Temps (CPU) |
|---|---|---|---|---|
| 1 → droite 4 | (4063,63 ; 30,25) | (4064,11 ; 31,27) | 828 | 31 s |
| 2 → bas 1 | (23,59 ; 4086,64) | (23,79 ; 4086,11) | 818 | 20 s |
| 2 → droite 3 | (4070,87 ; -18,54) | (4071,12 ; -18,76) | 837 | 17 s |
| 3 → bas 4 | (23,75 ; 4133,40) | (23,81 ; 4133,83) | 882 | 15 s |

L'écart maximal est de 1,02 pixel. La corrélation de phase sert ici de référence indépendante, mais ce n'est
pas une vérité terrain.
