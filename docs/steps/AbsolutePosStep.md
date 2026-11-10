# AbsolutePosStep

`StepsNs::AbsolutePosStepClass`, enregistré sous `absolute-pos`, convertit les translations relatives des appariements en positions absolues dans le repère d'une tuile de référence. Il ne charge aucune image et ne relance ni le matcher ni le rejet des aberrants.

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | `tiles` | `Tiles` | `DataNs::TileList` | `import` |
| Entrée | `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` | `phase-correlation`, `sift` ou `lightglue` |
| Sortie | `positions` | `Positions` | `std::vector<DataNs::TilePositionData>` | `graphcut` | `dynamic-programming-dijkstra`|
| Sortie | `pairResiduals` | `PairResiduals` | `std::vector<DataNs::PairResidual>` | pas encore de consommateur |

- **Lit** dans `tiles` l'identifiant et le nom du fichier source, d'où il tire la ligne et la colonne. Aucune image n'est relue.
- **Lit** dans `matches` seulement `tileIdA`, `tileIdB`, `translationX` et `translationY`. Les points et les scores ne servent pas.
- **Écrit** dans `positions` une entrée par tuile : `tileId` (`rLL_cCC`, ligne et colonne) ; `position`, le coin haut-gauche de la tuile en pixels de pleine résolution, dans le repère de la tuile de référence placée en (0, 0) (les valeurs négatives sont possibles) ; et `rotationRadians`, toujours 0.
- **Écrit** dans `pairResiduals` une entrée par paire utilisée : l'écart entre la translation mesurée et les positions ajustées (voir [Écart de chaque paire après ajustement](#écart-de-chaque-paire-après-ajustement)).

`AbsolutePosOutput` regroupe `key`, `value`, `type` pour les positions, et `residualsKey`, `residuals`, `residualsType` pour les écarts. `getOutput()` donne les positions et les écarts détaillés ; `getResult()` ne contient qu'un résumé (`pairs`, `positions`, `failures`, `error`). Le type `Positions` est déjà pris en charge par les checkpoints et consommé par GraphCut.

Les identifiants numériques de `TileClass` relient les paires aux tuiles. Les noms des fichiers doivent finir par `_r<ligne>_c<colonne>.tif` ou `.tiff`. Le même parseur que GraphCut produit les identifiants de sortie normalisés, par exemple `r01_c02`. Les identifiants numériques et les coordonnées de grille doivent être uniques. Aucune lecture du fichier image n'est nécessaire.

Le format par défaut de `import` (`img_C…_R…`) ne convient donc pas à ce step, ni à `graphcut`, qui exige les mêmes noms. Pour des fichiers `t_r00_c01.tif`, indiquer à `import` un format dont le groupe 1 est la colonne et le groupe 2 la ligne :

```
t_r(?=\d+_c(\d+)\.tif$)(\d+)_c\d+\.tif
```

## Paramètre

`referenceTileId` est un entier, par défaut `-1`. Cette valeur sélectionne la tuile de plus petit identifiant numérique, indépendamment de l'ordre de `tiles`. Sinon, la valeur doit désigner une tuile présente dans l'entrée. Sa position est fixée à `(0, 0)`. Les positions des autres tuiles peuvent être négatives : le step ne translate pas les résultats vers une boîte englobante positive.

La validation des clés et des types est assurée par `StepClass::importConfig()` ; le step vérifie la présence de la référence et la cohérence des données.

## Calcul

Chaque paire fournit la contrainte :

```
positionB - positionA = (translationX, translationY)
```

Les translations doivent être exprimées en pixels pleine résolution, dans les repères des tuiles complètes. Ce sont les champs `featureMatches.translationX` et `translationY` ; les points bruts ne sont pas recalculés ni filtrés ici. Les correspondances aberrantes doivent être rejetées en amont.

Le solveur commun `PositioningNs::solveRelativePositions()` ajuste toutes les contraintes simultanément par SVD, à poids égaux. Une boucle de mesures incohérentes est donc ajustée globalement, contrairement à une simple propagation suivant un chemin dans le graphe. L'ancien `TilePositioner` utilise le même solveur pour conserver un seul calcul de moindres carrés.

Le type `PairMatches` ne transporte pas d'angle relatif : **ce step ne calcule pas les rotations**, et `rotationRadians` vaut zéro. Le solveur commun accepte aussi des angles additifs, pour préserver le comportement existant de `TilePositioner` ; il ne s'agit pas d'une optimisation générale de poses rigides avec composition de rotations.

La SVD utilise une matrice dense (nombre de contraintes × nombre de tuiles hors référence). Elle convient aux grilles usuelles du projet ; un très grand graphe demanderait un solveur creux.

## Écart de chaque paire après ajustement

Une fois les positions calculées, le step mesure, pour chaque paire utilisée, l'écart entre la translation mesurée et celle des positions ajustées :

```
résidu = (translationX, translationY) - (positionB - positionA)
```

Il le publie dans `pairResiduals`, une entrée `DataNs::PairResidual` (`core/data/pair_residual.h`) par paire utilisée, dans l'ordre des paires de `matches` :

- `tileIdA`, `tileIdB` : identifiants numériques des deux tuiles, comme dans `PairMatches` ;
- `residual` : `(rx, ry)`, en pixels de pleine résolution ;
- `residualNorm` : la norme de `residual`.

Un grand résidu indique une mesure en contradiction avec les autres : la paire est à vérifier. Les paires ignorées (`failures`) n'ont pas de résidu. Sans boucle dans le graphe des paires (par exemple une ligne de tuiles), chaque paire est la seule à relier ses deux tuiles et son résidu vaut 0. Le résidu mesure donc l'incohérence entre les mesures, pas l'erreur réelle de position.

Le type `PairResiduals` a sa sérialisation de checkpoint, enregistrée par le constructeur du step (`CheckpointClass::registerType`). Le fichier `pairResiduals.json` contient une entrée par paire :

```json
[ { "tileIdA": 0, "tileIdB": 1, "residual": [-1.0, 0.0], "residualNorm": 1.0 } ]
```

## Erreurs

Une paire faisant référence à une tuile inconnue, à elle-même ou à une translation non finie est ignorée et enregistrée dans `failures` : `{idA, idB, reason}`.

La résolution réussit seulement si toutes les tuiles restent reliées à la référence. Un graphe déconnecté n'a pas de placement absolu unique ; le step renvoie `false` avec un message explicite plutôt que de publier des coordonnées arbitraires. Une tuile unique est placée à l'origine même sans paire. Les entrées `tiles` et `matches` restent obligatoires, avec un tableau de paires vide dans ce cas.

Les sorties de l'exécution précédente sont vidées au début de chaque appel, y compris dans le DataStore. Aucun résultat partiel n'est publié en cas d'échec. Il n'y a pas de journal contenant les listes de points ou les positions complètes.

## Exemple JSON

À insérer après un step produisant `matches` :

```json
{
  "name": "absolute-pos",
  "parameters": [
    { "valueType": "int", "name": "referenceTileId", "value": -1 }
  ]
}
```

`buildPipeline()` vérifie que `tiles` et `matches` ont été produits en amont avec les bons types. La configuration complète exportée par le step ajoute automatiquement ses `inputs` et `outputs`.

## Vérification

Les tests synthétiques couvrent les translations horizontales et verticales signées, une boucle incohérente, une référence explicite, les paires invalides ignorées, les graphes déconnectés, le nettoyage des résultats, la tuile unique, les entrées invalides et l'enregistrement JSON. Les tests vérifient aussi la restauration des positions par checkpoint, les bornes du paramètre entier, les coordonnées de grille dupliquées, la stabilité de la référence après permutation des tuiles, le chaînage JSON avec un matcher existant et l'écart de chaque paire après ajustement sur une boucle incohérente, ainsi que sa restauration par checkpoint.

Le solveur est également testé avec une rotation relative pour vérifier le contrat utilisé par l'ancien positionneur.

```sh
ctest --test-dir build --output-on-failure -R 'AbsolutePosStep|PositionSolver'
```

L'issue #18 ne définit pas de format d'entrée plus précis. Ce contrat utilise donc les types déjà présents sur `main`. Un futur step de rejet devra publier les translations filtrées dans `PairMatches`, ou un adaptateur devra être ajouté si son contrat final diffère. Un prototype `position` existe sur `feature/full-2x2-pipeline` ; ce step reste indépendant de cette branche et emploie l'ajustement global.
