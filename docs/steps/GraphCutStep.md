# GraphCutStepClass

Code : `steps/graph_cut_step.h` / `.cpp` (namespace `StepsNs`). Algorithme : `algorithms/seamline/graph_cut/` (`GraphCutNs::ImagePretreatmentClass`, `GraphCutNs::GraphCutSeamFinderClass`).

Calcule les **lignes de raccord** (seamlines) entre les tuiles déjà placées, puis en déduit le **territoire** de chaque tuile : les polygones de la mosaïque dont les pixels seront pris dans cette tuile à l'assemblage. Le step ne produit pas d'image.

## Contrat

| Sens | Clé | Type JSON | Type C++ |
|---|---|---|---|
| Entrée | `tiles` | `Tiles` | `DataNs::TileList` |
| Entrée | `positions` | `Positions` | `std::vector<DataNs::TilePositionData>` |
| Sortie | `territories` | `Territories` | `std::vector<DataNs::TileTerritory>` |

`getResult()` : `territories` (nombre de territoires) ou `error`. Les erreurs sont aussi écrites sur `std::cerr`, en français.

## Paramètre

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `scale` | float | 1 | réduction des images avant la coupe de graphe, dans ]0, 1]. 1 = pleine résolution, le plus lent ; 0.25 = coupe calculée sur des images 4 fois plus petites |

## Fonctionnement

1. **Lecture** : chemins des images source (`TileClass::getSource()`) et positions.
2. **Contrôles** : au moins une tuile, autant de positions que de tuiles, `scale` dans ]0, 1].
3. **Prétraitement** (`ImagePretreatmentClass::processImages`) :
   - chaque position est retrouvée **par le nom du fichier** : `parseTileName` lit `_r<ligne>_c<colonne>.tif` et fabrique l'identifiant `rLL_cCC`, celui que produit `absolute-pos`. Une image qui ne suit pas ce nommage fait échouer le step (voir [ImportStep.md](ImportStep.md)) ;
   - les coins sont **arrondis au pixel**. Ils sont ensuite décalés pour que le plus petit soit en (0, 0) : les positions de `absolute-pos` peuvent être négatives, celles de la mosaïque ne le sont pas ;
   - **la rotation (`rotationRadians`) est ignorée.**
4. **Coupe de graphe** (`computeSeamline`) : la coupe de graphe d'OpenCV (`COST_COLOR`) est appliquée paire par paire, sur les images réduites de `scale`. Il n'y a jamais plus de deux tuiles en mémoire. Elle produit une **carte de provenance**, une image 8 bits de la taille de la mosaïque en pleine résolution, où chaque pixel porte le numéro de la tuile choisie (255 = aucune). D'où la limite : **moins de 255 tuiles**.
5. **Polygones** (`extractPolygons`) : les contours de chaque territoire, trous compris, sont extraits de la carte de provenance.

`computeSeamline` construit aussi un aperçu 8 bits de la mosaïque, `finalMosaic`, que le step **jette**. C'est aujourd'hui le seul endroit du code où une image de la mosaïque est fabriquée.

## Ce qui est écrit dans le checkpoint (`territories.json`)

```json
[
  { "tileIndex": 0,
    "polygons": [ [ [0, 0], [8650, 0], [8641, 6800], [0, 6800] ] ],
    "holes": [ [] ] },
  { "tileIndex": 1,
    "polygons": [ [ [8651, 0], [17400, 12], [17400, 6810], [8642, 6800] ] ],
    "holes": [ [] ] }
]
```

Les valeurs de cet exemple sont inventées ; la forme est celle de `saveTerritories` (`core/checkpoint/checkpoint_types.cpp`). Un vrai contour suit la couture pixel par pixel et compte bien plus de sommets.

- `tileIndex` : place de la tuile dans `tiles`, et non son identifiant `rLL_cCC`.
- `polygons[i]` : contour extérieur, en **pixels entiers de la mosaïque, pleine résolution**, dans le repère décalé de l'étape 3.
- `holes[i]` : trous du polygone `i`.

Les territoires sont conçus pour former un **pavage exact** : chaque pixel couvert appartient à une seule tuile. `GraphCutSeamFinderClass::verifyTiling` permet de le contrôler (polygones redessinés comparés à la carte de provenance), mais rien ne l'appelle sur main.

## Mémoire

Les images sont relues depuis les fichiers source, deux à la fois, puis réduites.

En revanche, la carte de provenance (1 octet par pixel) et l'aperçu (3 octets par pixel) font chacun **la taille de la mosaïque entière en pleine résolution**. Pour une mosaïque de 300 millions de pixels, c'est environ 300 Mo et 900 Mo. À surveiller sur les grandes œuvres.

## Qui utilise les territoires

Aucun step sur main. L'assemblage (PR #69, en cours) devra prendre, pour chaque pixel de la mosaïque, la tuile dont le territoire le contient. Le front pourra dessiner les polygones pour montrer la vraie seamline.

## Erreurs

`execute()` renvoie `false`, avec la cause dans `error`, si :

- une entrée est illisible ;
- il n'y a aucune tuile ;
- le nombre de positions diffère du nombre de tuiles ;
- `scale` est hors de ]0, 1] ;
- le prétraitement échoue (nom de fichier non reconnu, position manquante, image illisible) ;
- la coupe échoue (255 tuiles ou plus, image illisible).

## Tests

Il n'y a pas de test du step lui-même. `tests/core/test_pipeline.cpp` vérifie seulement que `graphcut` est refusé sans step qui produit `positions`. L'algorithme seul n'a pas non plus de test unitaire sur main : les tests de `tests/seamline/` portent sur DP/Dijkstra.
