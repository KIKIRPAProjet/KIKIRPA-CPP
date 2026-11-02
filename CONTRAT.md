# Contrats du pipeline

Ces contrats constituent la convention commune du backend. Ils restent à faire
confirmer par les responsables métier avant d'être considérés comme approuvés.
Les types C++ partagés sont définis dans `include/Backend/core/Types.h`; les
clés du DataStore sont centralisées dans `stitching::keys`.

## Données échangées

| Clé DataStore | Type C++ | Producteur | Consommateurs |
|---|---|---|---|
| `tiles` | `Tiles` (`vector<TileImage>`) | `import` | `stitching`, `absolutePositioning`, `intensityCorrection`, `seamline`, `fusion` |
| `matches` | `Matches` (`vector<PairMatches>`) | `stitching` | disponible pour les étapes suivantes |
| `alignments` | `Alignments` (`vector<PairAlignment>`) | `stitching` | `absolutePositioning` |
| `positions` | `Positions` (`vector<TilePosition>`) | `absolutePositioning` | `seamline`, `fusion` |
| `corrections` | `LightCorrections` | `intensityCorrection` | `fusion` |
| `territories` | `Territories` | `seamline` | `fusion` |
| `mosaic` | `Mosaic` | `fusion` | `export` |

`TileImage.col` et `.row` sont des coordonnées de grille entières, positives,
lues comme groupes 1 et 2 de la regex de nommage. L'import vérifie l'unicité et
la complétude du rectangle de coordonnées. Les tuiles sont indexées de gauche à
droite puis de haut en bas.

Les coordonnées d'image OpenCV sont `(x, y) = (colonne, ligne)` en pixels,
l'origine est le coin supérieur gauche et `x` croît vers la droite, `y` vers le
bas. Les points d'une paire sont ordonnés `firstPoints[i]` puis
`secondPoints[i]`. `secondToFirst` transforme les pixels de la seconde tuile
vers ceux de la première; `tileToWorld` transforme les pixels d'une tuile vers
le repère de la mosaïque. Les matrices sont homogènes 3×3 en `double`.

Les corrections sont appliquées par canal sous la forme `pixel * gain + bias`.
Les territoires sont des masques `CV_8U` (0 hors territoire, 255 dedans). La
mosaïque est stockée en `CV_32F`; ses blocs décrivent les rectangles de
destination et les contributions de tuiles warpées. L'export prend en charge les formats
reconnus par l'extension via OpenCV; `bits` vaut 8 ou 16.

## Configuration et erreurs

Le format est un objet `{"pipeline":{"steps":[...]}}`. Chaque step a un nom
enregistré et des options `{ "type", "key", "value" }`. Les types JSON admis
sont `string`, `int`, `float` et `bool`. Les options non fournies conservent
leur valeur par défaut; options inconnues, doublons, types incompatibles,
steps inconnus et entrées sans producteur antérieur sont rejetés avec le nom
du step et de l'option ou de la clé concernés.

Les chemins relatifs du JSON sont résolus depuis le dossier contenant le script.
Les échecs de configuration lancent une exception descriptive. Les échecs
d'exécution renvoient `false`, inscrivent leur cause dans `exportData()["error"]`
et arrêtent le pipeline; le lanceur l'affiche sur stderr.

## Implémentation actuelle

La chaîne disponible est `import` → `stitching` → `absolutePositioning` →
`intensityCorrection` → `seamline` → `fusion` → `export`. Le matcher autorise
SIFT ou ORB avec rejet RANSAC; les options non implémentées sont refusées
explicitement: seam finder autre que `none` et blending autre que `average`.
Le calcul des territoires est actuellement le masque d'image (ou le canal alpha),
et la correction lumineuse est un équilibrage des moyennes par tuile.

Le fichier `pipeline.json` configure cette chaîne, mais le dépôt ne fournit pas
le jeu de tuiles `images/`. Son exécution réelle sur 2×2 doit donc être faite
avec quatre images raccordables avant de déclarer l'intégration complète.
