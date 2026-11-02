# SiftStepClass

Appariement SIFT des voisines droite et inférieure, enregistré sous `sift` dans le registre
`core/pipeline/pipeline_builder.cpp`. Classe : `StepsNs::SiftStepClass`, fichiers
`steps/sift_step.h` et `steps/sift_step.cpp`.

## Script JSON

```json
{
  "pipeline": {
    "steps": [
      {"name": "import", "parameters": [{"valueType": "string", "name": "path", "value": "images/"}]},
      {"name": "sift", "parameters": [
        {"valueType": "float", "name": "overlap", "value": 0.2},
        {"valueType": "int", "name": "maxFeatures", "value": 500},
        {"valueType": "float", "name": "loweRatio", "value": 0.75},
        {"valueType": "float", "name": "contrastThreshold", "value": 0.01},
        {"valueType": "float", "name": "claheClipLimit", "value": 2.0},
        {"valueType": "int", "name": "minMatches", "value": 4}
      ]}
    ]
  }
}
```

Les valeurs montrées sont les défauts. Les paramètres absents gardent leur valeur par défaut.
`StepClass::importConfig()` vérifie les noms, types et doublons. Le step contrôle les contraintes
métier : `overlap` et `loweRatio` finis dans `(0, 1)`, `maxFeatures` et `minMatches` entiers dans
`[4, INT_MAX]`, `minMatches <= maxFeatures`, `contrastThreshold` fini et positif,
`claheClipLimit` fini et non négatif. Zéro désactive CLAHE.

Le main réorganisé utilise `parameters` / `valueType` / `name`, et non l'ancien format
`options` / `type` / `key`. Les paramètres snake_case internes de `SiftMatcher` ne changent pas :
le step assure la traduction. Les scripts ne doivent plus utiliser `max_features`, `lowe_ratio`, etc.
Il n'existe plus de cible `stitching_run` sur ce main ; utiliser `PipelineNs::buildPipeline()` ou
`buildPipelineFromFile()` depuis l'application.

## Traitement et coordonnées

Entrée : `DataNs::TileList` sous `tiles`. Le step valide le graphe, lit les images sources en
8/16 bits non signés (1, 3 ou 4 canaux), puis appelle `EstimationRecouvrement::estimer()` et
`zonesCommunes()`. Seules les bandes communes sont transmises à SIFT.

Les points retournés sont ramenés dans les repères des deux tuiles en ajoutant l'origine de chaque
rectangle. La différence des origines est ajoutée à `translationX` et `translationY`.
La translation de B dans A est donc `pointsInA - pointsInB`, en pleine résolution.

Sortie : `vector<DataNs::PairMatches>` sous `matches`. `getOutput()` retourne `SiftOutput`,
avec `key`, `value`, `type`. Sa conversion JSON explicite conserve les correspondances détaillées
(`tileIdA`, `tileIdB`, `direction`, `matches`, `pointsInA`, `pointsInB`, `confidenceScores`,
`translationX`, `translationY`). Aucun RANSAC ni assemblage n'est réalisé.

## Échecs et journal

Une paire non mesurable est ignorée et listée dans `getResult()["failures"]` avec
`idA`, `idB`, `reason`. Les autres paires continuent, y compris après un fichier illisible.
Le step renvoie `false` si aucune paire ne réussit, ou en cas d'erreur globale de configuration
ou de graphe. La sortie précédente est remplacée par une liste vide au début du traitement.

`getResult()` (anciennement `exportData()`) est un résumé :

```json
{"pairs": 3, "failures": [{"idA": 0, "idB": 2, "reason": "overlap estimation failed"}]}
```

Un échec ajoute `error`. Les points restent dans le DataStore, le checkpoint et `getOutput()` ;
ils ne sont pas recopiés dans le journal.

## Fonctions communes

`steps/matching_step_utils.h` et `.cpp` regroupent `collectPairs`, `readImage`, `validateMatches`,
`restoreCoordinates` et `serializeOutput`. SIFT délègue ces opérations aux helpers.
Ils reprennent les helpers de la PR LightGlue en les migrant vers les types du main actuel
(`DataNs`, `StepsNs`, chemins en minuscules). La PR LightGlue, encore basée sur l'ancienne API,
doit utiliser ce même module lors de sa migration ; ne pas garder les deux variantes après fusion.

## Tests

`tests/steps/CMakeLists.txt` enregistre les tests GoogleTest, sans ajout dans l'ancien
`cmake/algorithms.cmake`, supprimé par main.

```sh
cmake -S . -B build/sift -DBUILD_TESTING=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build/sift --target test_sift_step pipeline_tests
ctest --test-dir build/sift -R 'SiftStepTest|Pipeline' --output-on-failure
```

Les tests emploient de vraies textures synthétiques et de vrais TIFF 8/16 bits : directions,
grille, sérialisation, paramètres, succès partiel, échec total et relance, import puis SIFT,
construction depuis JSON et aller-retour de configuration. Ils nécessitent OpenCV, libtiff,
nlohmann_json, GoogleTest et les dépendances du CMake racine (dont Qt6).
