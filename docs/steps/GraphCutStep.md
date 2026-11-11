# GraphCutStepClass

Code : `steps/graph_cut_step.h` / `.cpp` (namespace `StepsNs`). Algorithme : `algorithms/seamline/graph_cut/` (`GraphCutNs::ImagePretreatmentClass`, `GraphCutNs::GraphCutSeamFinderClass`).

Calcule les **lignes de raccord** (seamlines) entre les tuiles déjà placées, puis en déduit le **territoire** de chaque tuile : les polygones de la mosaïque dont les pixels seront pris dans cette tuile à l'assemblage. Le step ne produit pas d'image.

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | `tiles` | `Tiles` | `DataNs::TileList` | `import` |
| Entrée | `positions` | `Positions` | `std::vector<DataNs::TilePositionData>` | `absolute-pos` |
| Sortie | `territories` | `Territories` | `std::vector<DataNs::TileTerritory>` | aucun step sur main : l'assemblage (PR #69) |

- **Lit** dans `tiles` le chemin de chaque image source, relue deux à la fois puis réduite de `scale`.
- **Lit** dans `positions` la position de chaque tuile, retrouvée par le nom `rLL_cCC` tiré du nom de fichier. La rotation est ignorée.
- **Écrit** dans `territories` une entrée par tuile : `tileIndex` (sa place dans `tiles`), `polygons` et `holes`, en pixels entiers de la mosaïque pleine résolution (voir « Ce qui est écrit dans le checkpoint »).

`getResult()` : `territories` (nombre de territoires) ou `error`. Les messages d'erreur sont en anglais et ne sont pas écrits sur la console : ils passent uniquement par `result`.

## Paramètre

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `scale` | float | 1 | réduction des images avant la coupe de graphe, dans ]0, 1]. 1 = pleine résolution, le plus lent ; 0.25 = coupe calculée sur des images 4 fois plus petites |

## Fonctionnement

1. **Lecture** : chemins des images source (`TileClass::getSource()`) et positions.
2. **Contrôles** : au moins une tuile, autant de positions que de tuiles, `scale` dans ]0, 1].
3. **Prétraitement** (`ImagePretreatmentClass::processImages`) :
   - chaque position est retrouvée **par le nom du fichier** : `parseTileName` lit la fin du nom `_C<colonne>_R<ligne>.tif` (ou `.tiff`, casse indifférente) et fabrique l'identifiant `rLL_cCC` (ligne, colonne, deux chiffres au minimum), celui que produit `absolute-pos`. Exemple : `tile_C07_R03.tif` donne `r03_c07`. Une image qui ne suit pas ce nommage fait échouer le step (voir [ImportStep.md](ImportStep.md)) ;
   - les coins sont **arrondis au pixel**. Ils sont ensuite décalés pour que le plus petit soit en (0, 0) : les positions de `absolute-pos` peuvent être négatives, celles de la mosaïque ne le sont pas ;
   - **la rotation (`rotationRadians`) est ignorée.**
4. **Coupe de graphe** (`computeSeamline`) : la coupe de graphe d'OpenCV (`COST_COLOR`) est appliquée paire par paire, sur les images réduites de `scale`. Chaque tuile est lue et réduite une seule fois, à sa première paire, puis libérée dès qu'aucune paire suivante ne l'utilise. Elle produit une **carte de provenance**, une image 8 bits de la taille de la mosaïque en pleine résolution, où chaque pixel porte le numéro de la tuile choisie (255 = aucune). D'où la limite : **moins de 255 tuiles**.
5. **Polygones** (`extractPolygons`) : les contours de chaque territoire, trous compris, sont extraits de la carte de provenance.

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

Les territoires sont conçus pour former un **pavage exact** : chaque pixel couvert par au moins une tuile appartient à une seule tuile, et un pixel couvert par aucune tuile reste sans territoire (`NO_TILE`). Les tests `TerritoriesTileTheMosaicExactly` et `EveryPixelGoesToATileThatCoversIt` le contrôlent à plusieurs valeurs de `scale`.

## Mémoire

Chaque tuile est lue depuis son fichier source et réduite de `scale` **une seule fois**, à sa première paire, puis gardée tant qu'une paire à venir en a besoin et libérée après sa dernière paire. Les paires sont traitées dans l'ordre des tuiles (`i`, puis `j > i`) ; avec le format par défaut d'`import` (`img_C…_R…`, trié par nom, donc colonne par colonne), il reste en mémoire environ une colonne de tuiles réduites, plus deux. Une tuile qui ne chevauche aucune autre n'est jamais lue.

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

`tests/seamline/test_graph_cut.cpp` couvre le step et l'algorithme. Les tuiles sont des images 16 bits synthétiques, écrites dans un dossier temporaire propre à chaque test, avec des noms `tile_C<colonne>_R<ligne>.tif` : aucun fichier externe n'est nécessaire.

- **Contrôles du step** : aucune tuile, nombre de positions différent du nombre de tuiles, trop de tuiles (255), `scale` hors de ]0, 1].
- **Exécution** : un territoire par tuile et dans l'ordre des tuiles, écriture de `territories` dans le `DataStoreClass`, lecture de `tiles` et `positions` depuis ce même DataStore.
- **Erreurs de prétraitement** : fichier illisible, position manquante, nom de fichier qui ne finit pas par `_C<colonne>_R<ligne>.tif`.
- **Algorithme** : pavage exact et pixels attribués à une tuile qui les couvre, pour `scale` = 1, 0.5 et 0.3 ; pixels hors de toute tuile laissés à `NO_TILE` ; tuile seule ; message d'erreur qui nomme la tuile illisible et qui est effacé par un appel réussi.

`tests/core/test_pipeline.cpp` vérifie en plus que `graphcut` est refusé sans step qui produit `positions`.

Une tuile de test doit porter un nom accepté par `parseTileName`. Un nom comme `tile_r00_c00.tif` fait échouer le prétraitement, comme en production.
