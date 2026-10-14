# Stitching : positionnement SIFT + RANSAC puis GraphCut

Entrée : des tuiles `.tif` (16 bits) nommées `..._rLL_cCC.tif`. Aucune position n'est fournie : elle est calculée.

```
seamline_graphcut [--qualite=1] [--chevauchement=0.3] tuile_r00_c00.tif tuile_r00_c01.tif ...
positioning_test  [--chevauchement=0.3] tuile_r00_c00.tif ...      # positions seules, sans GraphCut
```

Chaîne : lecture 16 bits sans perte → SIFT sur la bande de recouvrement de chaque paire de voisins (droite, dessous) →
RANSAC → translation = médiane de (pointA - pointB) sur les inliers → ajustement global par moindres carrés →
`TilePositionData` → coupe de graphe (`Seamline::GraphCut`) → territoires et vérification du pavage.

Notes
- `cv::SIFT` n'accepte que du 8 bits : lecture, niveau de gris et CLAHE sont en 16 bits, seule l'entrée de SIFT est quantifiée (échelle fixe 65535 → 255).
- Une paire dont SIFT ou RANSAC échoue est ignorée ; si une tuile n'est plus reliée à la référence, le programme s'arrête avec un message.
- Le pipeline (`src/main.cpp`, `core/`, `steps/`, `app/`, `gui/`) reste un squelette pour plus tard.
