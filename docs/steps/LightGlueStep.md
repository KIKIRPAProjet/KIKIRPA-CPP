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

Chaque paire passe par `EstimationRecouvrement::estimer()`, puis `zonesCommunes()`.
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

OpenCV, libtiff, nlohmann_json, Qt6 et GoogleTest sont requis. ONNX Runtime est facultatif pour les
autres steps ; sans lui, l'exécution réelle de LightGlue retourne une erreur explicite.
La fabrique de matcher permet de tester le step sans remplacer l'estimation réelle du recouvrement.

```sh
cmake -S . -B build/lightglue -DBUILD_TESTING=ON -DONNXRUNTIME_ROOT=/chemin/onnxruntime
cmake --build build/lightglue --parallel
ctest --test-dir build/lightglue --output-on-failure
```

Sur macOS, les bibliothèques peuvent être installées avec Homebrew :

```sh
brew install opencv libtiff nlohmann-json qtbase googletest onnxruntime
```

Les tests GoogleTest du step vérifient le découpage horizontal/vertical en 8/16 bits, le recalage,
les scores sous 0.9, la sérialisation, les succès partiels, les erreurs d'images, les relances,
les paramètres, le registre JSON et l'échec de chargement du vrai matcher.

Pour le test d'inférence complet, fournir un modèle compatible :

```sh
cmake -S . -B build/lightglue -DBUILD_TESTING=ON -DLIGHTGLUE_TEST_MODEL=/chemin/modele.onnx
cmake --build build/lightglue --target test_lightglue_model
ctest --test-dir build/lightglue -R '^lightglue_model$' --output-on-failure
```

Ce dernier test utilise une translation connue et vérifie les correspondances 8/16 bits. Il n'est
pas enregistré sans modèle et ne doit pas être présenté comme exécuté dans ce cas.
