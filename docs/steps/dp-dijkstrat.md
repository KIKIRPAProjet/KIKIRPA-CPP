# Step `dynamic-programming-dijkstra`

## Rôle

Ce step calcule une seamline dans chaque recouvrement entre tuiles voisines. Il fait le lien entre le pipeline et
l'algorithme existant `DynamicProgrammingDijkstraNs::SeamFinderClass` ; il ne réimplémente ni la carte de coût, ni
les solveurs DP/Dijkstra.

Le step est enregistré sous le nom `dynamic-programming-dijkstra` dans `pipeline_builder.cpp`.

## Contrat pipeline

| | Type C++ | Type pipeline | Clé DataStore |
|---|---|---|---|
| Entrée | `DataNs::TileList` | `Tiles` | `tiles` |
| Entrée | `std::vector<DataNs::TilePositionData>` | `Positions` | `positions` |
| Entrée | `DataNs::IntensityCorrection` | `IntensityCorrection` | `intensityCorrection` |
| Sortie | `DataNs::Seamlines` (`std::vector<DataNs::Seamline>`) | `Seamlines` | `seamlines` |

Les paramètres sont facultatifs dans le script : en leur absence, les valeurs par défaut du step sont conservées.

| Paramètre | Type JSON | Défaut | Valeurs / rôle |
|---|---|---:|---|
| `method` | `string` | `"DP"` | `"DP"`, `"Dijkstra-8"` ou `"DP+Dijkstra-local"` |
| `costFunction` | `string` | `"color_gradient"` | Nom d'une fonction du catalogue de coûts (voir ci-dessous) |
| `corridorRadius` | `int` | `18` | Rayon en pixels du corridor, utilisé uniquement par `"DP+Dijkstra-local"` ; doit être positif ou nul |

Exemple de configuration du step :

```json
{
  "name": "dynamic-programming-dijkstra",
  "parameters": [
    { "valueType": "string", "name": "method", "value": "DP+Dijkstra-local" },
    { "valueType": "string", "name": "costFunction", "value": "color_gradient" },
    { "valueType": "int", "name": "corridorRadius", "value": 18 }
  ]
}
```

Le pipeline vérifie que les entrées déclarées sont produites par des steps précédents et que leurs types correspondent.
Le step d'égalisation n'étant pas encore enregistré dans le projet, un pipeline qui utilise ce step ne peut être construit
qu'une fois qu'un producteur de `intensityCorrection` aura été intégré. Il faut également un producteur de `positions`.
La note d'intégration fournie propose :

```text
import → phase-correlation → intensity-correction → (positions) → dynamic-programming-dijkstra
```

Les parenthèses signalent que le placement exact de l'étape qui produit `positions` reste à confirmer avec son contrat.
Quel que soit cet ordre, les trois données `tiles`, `positions` et `intensityCorrection` doivent exister avant ce step.

## Traitement effectué

Pour chaque tuile, le step examine uniquement sa voisine de droite puis celle du dessous, à partir des pointeurs de
voisinage de `TileClass`. Chaque paire d'adjacentes n'est donc traitée qu'une fois.

Pour chaque paire A/B :

1. Les images d'origine sont lues en pleine résolution avec `cv::IMREAD_UNCHANGED`.
2. Le recouvrement commun est déterminé à partir de la différence entre les positions de B et A, via
   `OverlapEstimatorClass::computeCommonAreas`. Cette fonction arrondit la translation au pixel pour découper deux
   rectangles de même taille.
3. Chaque copie du recouvrement est divisée par le gain correspondant à la tuile. Les images d'origine ne sont pas
   modifiées ; `convertTo` conserve leur type et sature les valeurs pour les images entières.
4. Les images couleur lues par OpenCV (BGR) sont converties en RGB, convention attendue par l'algorithme. Les images
   en niveaux de gris restent en un canal.
5. Pour une paire A/B verticale (B en dessous), les deux recouvrements sont transposés avant l'appel à
   `SeamFinderClass::computeSeam`, qui résout toujours un chemin du haut vers le bas.
6. Les points du chemin sont ramenés dans le repère absolu de la mosaïque, avec la position de A et l'origine du
   recouvrement dans A.

L'algorithme utilise `CostMapParameters` avec ses valeurs par défaut ; seuls la fonction de coût et la méthode de
résolution sont configurables dans ce step. Les fonctions de coût connues du catalogue sont :
`color_gradient`, `color_only`, `gradient_only`, `grain_robust`, `local_norm`, `highlight_penalty` et `center_prior`.

## Donnée d'égalisation attendue

Le futur step `intensity-correction` doit publier sous la clé `intensityCorrection` un objet
`DataNs::IntensityCorrection` contenant :

- `tileGains` : un gain `double` strictement positif par tuile, dans le même ordre que `tiles`. Le pixel corrigé est
  calculé en divisant par ce gain.
- `fieldStrength` : coefficient du champ d'éclairage. Il est conservé dans la donnée, mais n'est pas utilisé pour
  calculer la seamline ; son application est réservée à l'assemblage final.
- `isolatedTileIndices` : indices des tuiles sans mesure d'égalisation. Le step de couture n'a pas de traitement
  spécial pour ces indices : il applique simplement leur gain, qui vaut normalement 1.

La taille de `tileGains` doit correspondre à `tiles.size()`. La liste des positions doit elle aussi avoir exactement
une entrée par tuile, dans le même ordre que `tiles`. Le step utilise l'indice de la liste pour sélectionner gains
et positions ; il ne réordonne pas ces données selon `tileId` ou `tileIdA`.

## Sortie

`DataNs::Seamlines` contient une entrée par paire de voisines traitée. Chaque `DataNs::Seamline` contient :

- `tileIdA` et `tileIdB` : identifiants des deux tuiles ;
- `direction` : `DataNs::Direction::Right` ou `DataNs::Direction::Bottom` ;
- `points` : coordonnées `cv::Point2d` des pixels du chemin, en pleine résolution et dans le repère absolu de la
  mosaïque.

Le résultat est également disponible via `getOutput().seamlines`. Le compte rendu du step contient le nombre de
coutures, la méthode et la fonction de coût. En cas d'échec, `execute()` renvoie `false` et place la cause dans
`getResult()["error"]`.

Le type `Seamlines` et le type d'entrée `IntensityCorrection` sont enregistrés pour la sauvegarde et la reprise par
checkpoint.

## Échecs et limites connues

Le step échoue notamment si les entrées ou paramètres sont illisibles, si la liste des tuiles est vide, si les tailles
de `positions` ou `tileGains` ne correspondent pas à `tiles`, si un gain n'est pas fini et strictement positif, si un
recouvrement est vide, si une image est illisible ou d'un type non pris en charge, ou si le calcul algorithmique échoue.
Une paire voisine absente est simplement ignorée. Si aucune paire n'est présente, le step peut réussir avec une liste
`seamlines` vide.

Le step suppose que les tuiles sont axis-alignées : `rotationRadians` n'est pas appliqué pour découper les overlaps ni
pour replacer les points. Il relit les fichiers sources et ne consomme pas les pixels éventuellement corrigés par un
autre step. La sortie est le réseau de chemins par paire, pas des territoires de tuiles ni une mosaïque assemblée.

## Code et références du contrat

- Implémentation du step : `include/backend/steps/dynamic_programming_dijkstra_step.h` et
  `src/backend/steps/dynamic_programming_dijkstra_step.cpp`.
- Enregistrement dans le pipeline : `src/backend/core/pipeline/pipeline_builder.cpp`.
- Algorithme appelé : `include/backend/algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h`.
- Types échangés : `include/backend/core/data/algorithm_types.h`.
- Données et ordre attendus du futur step d'égalisation : `egalisation-step.md` et
  `IntensityCorrection_pour_seamline.md` à la racine du dépôt. Ces notes sont des documents de travail fournis avec
  le projet ; confirmer avec leurs auteurs si le contrat évolue.