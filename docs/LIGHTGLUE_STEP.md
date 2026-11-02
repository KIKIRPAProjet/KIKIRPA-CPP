# Étape LightGlue

`LightGlueStep` mesure les correspondances entre les voisins droits et inférieurs des `Tiles`
(`vector<shared_ptr<Tuile>>`) importées. Le registre du pipeline expose le nom `lightglue`.

## Configuration JSON

```json
{
  "pipeline": {
    "steps": [
      {"name": "import", "options": [{"key": "path", "type": "string", "value": "images/"}]},
      {"name": "lightglue", "options": [
        {"key": "modelPath", "type": "string", "value": "/chemin/modele.onnx"},
        {"key": "minMatches", "type": "int", "value": 4},
        {"key": "overlap", "type": "float", "value": 0.2}
      ]}
    ]
  }
}
```

Les valeurs absentes gardent les valeurs du constructeur : `modelPath` vide, `minMatches = 4`,
`overlap = 0.2`. `Step::importConfig()` contrôle la structure, les noms, types et doublons.
Le step contrôle uniquement les contraintes métier : modèle non vide, minimum positif dans
la plage `int`, recouvrement fini strictement entre 0 et 1. Les anciennes options
`model_path` et `min_matches` ne sont plus acceptées dans les scripts. Le paramètre interne
`model_path` du constructeur de `LightGlueMatcher` reste traduit par le step.

## Traitement des paires

1. Valider les tuiles et leurs voisins, sans traiter deux fois une même paire.
2. Lire les images sources (8/16 bits non signés, 1/3/4 canaux).
3. Appeler `EstimationRecouvrement::estimer()` avec le recouvrement nominal.
4. Calculer les rectangles réels avec `zonesCommunes()` et transmettre uniquement ces bandes au matcher.
5. Valider les correspondances dans les bandes, puis ajouter l'origine de chaque rectangle
   aux points de sa tuile. Ajouter la différence des origines à `(dx, dy)`.
6. Vérifier les résultats dans les images complètes et conserver la paire réussie.

La réduction à 3200 pixels s'applique maintenant aux bandes, et non à la tuile complète.
Une bande de plus de 3200 pixels sur son grand côté reste réduite selon les contraintes du modèle.
Les scores sont acceptés s'ils sont finis et dans `[0, 1]` ; le seuil de sélection appartient au matcher.
Aucun RANSAC, placement global ou assemblage n'est effectué ici.

Une erreur de lecture, d'estimation, d'inférence ou de validation d'une paire est inscrite dans
`failures` et les autres paires sont traitées. Si aucune paire ne réussit, `execute()` renvoie
`false`. Les erreurs globales (configuration, graphe invalide, chargement du modèle) arrêtent le step.
La sortie précédente est retirée au début de chaque exécution.

## Sorties et journal

Le `DataStore` contient `matches` de type `vector<stitching::PairMatches>`.
`getOutput()` retourne `LightGlueOutput`, avec `key = "matches"`, `value` (les paires) et
`type = "PairMatches"`. Sa conversion JSON explicite conserve tous les points, scores,
identifiants, directions et translations.

`exportData()` est uniquement un résumé destiné au journal, par exemple :

```json
{"pairs": 3, "failures": [{"idA": 0, "idB": 2, "reason": "overlap estimation failed"}]}
```

Un échec global ou l'absence de paire réussie ajoute `error`. Les points ne sont jamais
copiés dans ce résumé. Les sorties détaillées restent accessibles via le DataStore et `getOutput()`.
Le modèle est réutilisé et rechargé lorsque `modelPath` change.

## Fonctions communes et SIFT

`steps/MatchingStepUtils.h` et son implémentation fournissent `collectPairs`, `readImage`,
`validateMatches`, `restoreCoordinates` et `serializeOutput`. Les enveloppes de sortie propres
à chaque step délèguent leur sérialisation à cette dernière fonction.
La PR SIFT étant indépendante, son adaptation doit reprendre ces mêmes fonctions ; elle ne doit
pas réintroduire les implémentations privées dupliquées. Les fichiers communs sont indépendants
d'ONNX Runtime.

## Tests

Les cibles LightGlue sont dans `cmake/lightglue-tests.cmake`, pour éviter de modifier le bloc de
`cmake/algorithms.cmake` où SIFT ajoute ses tests.

```sh
cmake -S . -B build/lightglue -DBUILD_TESTING=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build/lightglue --target test_lightglue_step test_pipeline
ctest --test-dir build/lightglue -R '^(lightglue_step|pipeline)$' --output-on-failure
```

Ces tests nécessitent OpenCV, libtiff et nlohmann_json. Le test d'orchestration utilise un matcher
contrôlé, mais la vraie estimation du recouvrement sur des textures synthétiques 8/16 bits.
Il vérifie les bandes horizontales/verticales, les coordonnées et translations, les succès
partiels, l'absence de données périmées, le résumé et la construction depuis un script JSON.

Le test réel ONNX est disponible avec ONNX Runtime, OpenCV dnn et
`-DLIGHTGLUE_TEST_MODEL=/chemin/modele.onnx`. Sans ce modèle, il n'est pas exécuté.
