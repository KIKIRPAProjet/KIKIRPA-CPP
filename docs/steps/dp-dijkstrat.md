# Step `dynamic-programming-dijkstra`

## Rôle

Ce step calcule une couture dans chaque recouvrement entre tuiles voisines,
puis construit les territoires utilisés par [FusionStep](FusionStep.md).

Il produit deux sorties complémentaires :

- `seamlines` : les chemins originaux calculés par DP/Dijkstra ;
- `territories` : les zones attribuées aux tuiles pour l'assemblage.

La construction des territoires conserve les coutures originales.

Le step utilise `DynamicProgrammingDijkstraNs::SeamFinderClass` pour calculer
les chemins et `DynamicProgrammingDijkstraNs::buildTerritories()` pour
construire les territoires. Il ne réimplémente ni les cartes de coût ni les
solveurs DP/Dijkstra.

Il est enregistré sous le nom `dynamic-programming-dijkstra` dans
`pipeline_builder.cpp`.

## Contrat pipeline

| Sens | Type C++ | Type pipeline | Clé DataStore |
|---|---|---|---|
| Entrée | `DataNs::TileList` | `Tiles` | `tiles` |
| Entrée | `std::vector<DataNs::TilePositionData>` | `Positions` | `positions` |
| Entrée | `DataNs::IntensityCorrection` | `IntensityCorrection` | `intensityCorrection` |
| Sortie | `DataNs::Seamlines` | `Seamlines` | `seamlines` |
| Sortie | `std::vector<DataNs::TileTerritory>` | `Territories` | `territories` |

Les trois entrées doivent être disponibles avant l'exécution du step.

Exemple d'enchaînement :

```text
import
  → phase-correlation
  → absolute-pos
  → intensity-correction
  → dynamic-programming-dijkstra
  → fusion
  → export
```

FusionStep consomme `territories`. Les coutures restent disponibles sous
`seamlines`, même si FusionStep ne les lit pas directement.

## Paramètres

Les paramètres sont facultatifs dans le script. En leur absence, les valeurs
par défaut du step sont conservées.

| Paramètre | Type JSON | Défaut | Rôle |
|---|---|---|---|
| `method` | `string` | `"DP"` | `"DP"`, `"Dijkstra-8"` ou `"DP+Dijkstra-local"` |
| `costFunction` | `string` | `"color_gradient"` | Fonction du catalogue de coûts |
| `corridorRadius` | `int` | `18` | Rayon en pixels du corridor de `"DP+Dijkstra-local"` ; positif ou nul |

Exemple de configuration :

```json
{
  "name": "dynamic-programming-dijkstra",
  "parameters": [
    {
      "valueType": "string",
      "name": "method",
      "value": "DP+Dijkstra-local"
    },
    {
      "valueType": "string",
      "name": "costFunction",
      "value": "color_gradient"
    },
    {
      "valueType": "int",
      "name": "corridorRadius",
      "value": 18
    }
  ]
}
```

Les fonctions de coût disponibles sont :

- `color_gradient` ;
- `color_only` ;
- `gradient_only` ;
- `grain_robust` ;
- `local_norm` ;
- `highlight_penalty` ;
- `center_prior`.

Le step utilise `CostMapParameters` avec ses valeurs par défaut.

## Identifiants et ordre des données

### Identifiant d'une tuile

L'identifiant d'une tuile est l'entier retourné par `TileClass::getId()`.

Cet identifiant est utilisé dans :

- `TilePositionData::tileId` ;
- `Seamline::tileIdA` ;
- `Seamline::tileIdB`.

Le step ne déduit pas l'identifiant ou la case d'une tuile depuis son nom de
fichier. Il utilise les pointeurs de voisinage établis par l'import.

La case dans la grille est distincte de l'identifiant :

- `getGridRow()` : ligne, à partir de zéro ;
- `getGridColumn()` : colonne, à partir de zéro.

### Positions

Chaque entrée de `positions` associe une position à un identifiant entier :

```cpp
DataNs::TilePositionData{
    tile->getId(),
    cv::Point2d(positionX, positionY),
    0.0
};
```

L'ordre d'entrée des positions est libre.

`orderTilePositions()` les associe aux tuiles par `tileId`, puis les range dans
l'ordre de `tiles` avant le calcul des coutures.

Le constructeur de territoires effectue également cette association lorsqu'il
est appelé directement.

Une position manquante, un identifiant dupliqué ou une association ne
correspondant pas aux tuiles provoque un échec.

### Indice d'un territoire

`TileTerritory::tileIndex` reste l'indice de la tuile dans `tiles`.

Il ne doit pas être remplacé par `TileClass::getId()` : un identifiant et un
indice sont deux notions distinctes, même si leurs valeurs peuvent coïncider
dans un pipeline issu de l'import.

## Donnée d'égalisation attendue

Le step `intensity-correction` publie sous la clé `intensityCorrection` un objet
`DataNs::IntensityCorrection`.

Le step de couture utilise :

- `tileGains` : un gain fini et strictement positif par tuile, dans l'ordre de
  `tiles`. La taille doit être égale à `tiles.size()`. Les pixels sont corrigés
  en divisant par ce gain.

Les autres champs sont conservés dans la donnée :

- `fieldStrength` : coefficient du champ d'éclairage, non appliqué lors du
  calcul des coutures ; son application est réservée à l'assemblage ;
- `isolatedTileIndices` : indices des tuiles sans mesure d'égalisation. Le step
  de couture applique leur gain comme celui des autres tuiles ; ce gain vaut
  normalement 1.

Contrairement aux positions, les gains ne sont pas réordonnés par identifiant :
ils doivent déjà suivre l'ordre de `tiles`.

## Calcul des coutures

Pour chaque tuile, le step examine sa voisine de droite puis celle du dessous.
Chaque paire est ainsi traitée une seule fois.

Pour une paire A/B :

1. Les images sources sont lues en pleine résolution avec
   `IONs::readImage(..., cv::IMREAD_UNCHANGED)`.
2. Le recouvrement est déterminé à partir de la différence des positions de B
   et A, avec `OverlapEstimatorClass::computeCommonAreas()`. La translation est
   arrondie au pixel pour découper deux régions de même taille.
3. Chaque recouvrement est divisé par le gain de sa tuile. Les images sources
   restent inchangées. Le type est conservé ; les valeurs des images entières
   sont saturées par `convertTo()`.
4. Les images couleur sont converties de BGR en RGB, convention attendue par
   l'algorithme. Les images en niveaux de gris restent en un canal.
5. Si B est en dessous de A, les recouvrements sont transposés : le solveur
   cherche toujours un chemin du haut vers le bas.
6. `SeamFinderClass::computeSeam()` calcule le chemin.
7. La transposition éventuelle est annulée et les points sont replacés dans
   le repère absolu de la mosaïque.

Le type d'image doit être identique pour les deux tuiles de la paire :
un ou trois canaux, avec une profondeur `CV_8U`, `CV_16U` ou `CV_32F`.

## Construction des territoires

### Repère utilisé

Les territoires utilisent les mêmes coins entiers que FusionStep, calculés
par `DataNs::computeMosaicCorners()` :

1. soustraire le minimum des positions ;
2. arrondir chaque coin avec `cvRound`.

L'ordre de ces opérations est important. Arrondir les positions absolues avant
de soustraire le minimum pourrait décaler les territoires d'un pixel.

Les points d'une couture sont convertis dans ce repère relativement au coin
entier de la tuile A. Les points originaux de `seamlines` restent inchangés.

### Découpage d'une paire

Le constructeur conserve le chemin complet, y compris les retours en arrière
possibles de Dijkstra. Il ne réduit pas le chemin à une seule abscisse par ligne.

Pour obtenir le côté A, le chemin est fermé :

- vers le bord gauche du recouvrement si B est à droite ;
- vers le bord haut du recouvrement si B est en dessous.

Le polygone est rasterisé avec `cv::fillPoly()`.

Les pixels du chemin appartiennent au côté A. Le complément du masque dans le
recouvrement représente le côté B.

### Attribution dans les recouvrements multiples

Les tuiles sont insérées dans l'ordre de `tiles`.

Pour chaque nouvelle tuile :

1. les pixels couverts encore sans propriétaire lui sont attribués ;
2. les coutures avec les tuiles précédentes sont examinées dans leur ordre ;
3. une couture peut transférer un pixel à la nouvelle tuile uniquement si son
   propriétaire courant est l'autre tuile de cette paire et si le masque
   favorise la nouvelle tuile.

Un propriétaire tiers reste inchangé. Sans couture entre la nouvelle tuile et
le propriétaire courant, ce dernier reste prioritaire.

Chaque pixel couvert reçoit ainsi un seul propriétaire.

Cette règle dépend de l'ordre de `tiles`. Elle ne réalise pas une optimisation
globale des jonctions : dans les recouvrements de plusieurs tuiles, toutes les
coutures par paire ne deviennent pas nécessairement des frontières visibles
dans la mosaïque.

Les coutures originales restent disponibles dans `seamlines`.

### Traitement par blocs

L'attribution est calculée par blocs de `DataNs::BLOCK_SIZE`, soit 512 pixels
de côté. Les blocs des bords peuvent être plus petits.

Aucune carte de propriétaires couvrant toute la mosaïque n'est allouée.

Pour chaque bloc et chaque tuile concernée :

- un masque est construit depuis la carte de propriétaires ;
- les contours sont extraits avec `cv::RETR_CCOMP` et
  `cv::CHAIN_APPROX_SIMPLE` ;
- les trous sont rattachés à leur contour extérieur ;
- les coordonnées sont replacées dans le repère entier de la mosaïque.

Aucun filtre d'aire ne supprime les petits contours.

Les polygones restent fragmentés aux frontières des blocs. Un même territoire
peut donc contenir plusieurs polygones adjacents. Les limites entre fragments
d'une même tuile ne sont pas des coutures entre images.

La mémoire de travail des masques est limitée à un bloc. Les chemins, la
géométrie et les polygones de sortie restent en mémoire ; leur taille dépend
des données.

## Sorties

### `seamlines`

`DataNs::Seamlines` contient une entrée par paire de voisines traitée.

Chaque `DataNs::Seamline` contient :

- `tileIdA` et `tileIdB` : identifiants entiers des deux tuiles ;
- `direction` : `DataNs::Direction::Right` ou
  `DataNs::Direction::Bottom` ;
- `points` : coordonnées `cv::Point2d` du chemin, en pleine résolution et dans
  le repère absolu de la mosaïque.

Cette sortie est accessible dans le DataStore et via :

```cpp
step.getOutput().seamlines
```

### `territories`

`std::vector<DataNs::TileTerritory>` contient une entrée par tuile, dans l'ordre
de `tiles`.

Chaque `DataNs::TileTerritory` contient :

- `tileIndex` : indice de la tuile dans `tiles` ;
- `polygons` : contours extérieurs en coordonnées entières de la mosaïque ;
- `holes[i]` : trous du polygone `polygons[i]`.

Les listes `polygons` et `holes` ont la même taille. Une tuile sans pixel
attribué possède des listes vides.

Cette sortie est accessible dans le DataStore et via :

```cpp
step.getOutput().territories
```

FusionStep lit cette donnée pour sélectionner la tuile qui fournit chaque
pixel de la mosaïque.

### Compte rendu et publication

En cas de succès, `getResult()` contient :

- `seamlines` : nombre de coutures ;
- `territories` : nombre d'entrées de territoire ;
- `method` : méthode utilisée ;
- `costFunction` : fonction de coût utilisée.

Les deux sorties sont publiées après réussite du calcul des coutures et des
territoires.

En cas d'échec, `execute()` renvoie `false` et place la cause dans :

```cpp
step.getResult()["error"]
```

## Sauvegarde et reprise

Les types `Positions`, `IntensityCorrection`, `Seamlines` et `Territories` sont
pris en charge par le mécanisme de checkpoint.

Les anciens checkpoints de positions utilisant des identifiants sous forme
de chaînes, comme `"r00_c00"`, ne correspondent plus au contrat courant :
`TilePositionData::tileId` est désormais un entier.

Après cette migration, relancer le pipeline sans réutiliser ces anciens
checkpoints. Le positionnement, les coutures, les territoires et la fusion
doivent utiliser les mêmes tuiles et identifiants.

Un ancien résultat DP/Dijkstra ne contenant que `seamlines` doit également être
recalculé pour fournir `territories` à FusionStep.

## Échecs et limites connues

Le step échoue notamment si :

- le DataStore n'est pas attaché ;
- une entrée ou un paramètre est illisible ;
- la liste des tuiles est vide ;
- une tuile est nulle ou son identifiant est dupliqué ;
- les positions ne correspondent pas aux identifiants des tuiles ;
- une position n'est pas finie ;
- le nombre de gains diffère du nombre de tuiles ;
- un gain n'est pas fini ou n'est pas strictement positif ;
- le rayon du corridor est négatif ;
- une voisine référencée est absente de la liste des tuiles ;
- deux voisines ne se recouvrent pas ;
- une image est illisible ou son type n'est pas pris en charge ;
- le solveur échoue ou renvoie un chemin vide ;
- les dimensions de la mosaïque dépassent les coordonnées entières acceptées ;
- une couture fournie au constructeur de territoires est invalide.

Une voisine nulle est simplement ignorée. Sans paire de voisines, la liste
`seamlines` peut être vide ; les territoires sont alors attribués selon la
couverture des tuiles et leur ordre.

Les tuiles sont supposées alignées sur les axes. `rotationRadians` n'est pas
appliqué pour découper les recouvrements ou construire les territoires.

Le step relit les images sources. Il ne consomme pas une éventuelle version
des pixels corrigée par un autre step.

Les coutures sont calculées indépendamment par paire. Ce step n'utilise pas
le calcul de réseau global et de jonctions disponible dans `junctions.h`.

La mosaïque finale est construite par FusionStep, pas par ce step.

## Tests

`tests/seamline/test_territory_builder.cpp` utilise des tuiles synthétiques pour
vérifier :

- l'association par identifiants entiers, distincts des indices ;
- des positions fournies dans l'ordre inverse de `tiles` ;
- le changement de repère avec des positions négatives et fractionnaires ;
- la conservation des points des coutures ;
- la couverture de la mosaïque sans chevauchement entre les deux territoires ;
- le nombre de pixels attribués de chaque côté d'une couture connue.

Ce test ne valide pas à lui seul l'enchaînement complet
DP/Dijkstra → FusionStep, les chemins Dijkstra avec retours en arrière ou les
jonctions entre quatre tuiles.

## Code et références

- Step :
  `include/backend/steps/dynamic_programming_dijkstra_step.h` et
  `src/backend/steps/dynamic_programming_dijkstra_step.cpp`.
- Calcul des chemins :
  `include/backend/algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h`.
- Association des positions et construction des territoires :
  `include/backend/algorithms/seamline/dynamic_programming_dijkstra/territory_builder.h`
  et
  `src/backend/algorithms/seamline/dynamic_programming_dijkstra/territory_builder.cpp`.
- Types partagés :
  `include/backend/core/data/algorithm_types.h`.
- Identifiant et case des tuiles :
  `include/backend/core/data/tile.h`.
- Repère entier :
  `include/backend/core/data/mosaic_corners.h`.
- Enregistrement du step :
  `src/backend/core/pipeline/pipeline_builder.cpp`.
- Consommation des territoires :
  [FusionStep.md](FusionStep.md).