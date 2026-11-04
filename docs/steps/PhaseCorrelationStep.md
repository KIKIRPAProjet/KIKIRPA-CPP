# PhaseCorrelationStepClass

Code : `steps/phase_correlation_step.h` / `.cpp` (namespace `StepsNs`).

Mesure par corrélation de phase la position de chaque tuile par rapport à ses voisines de droite et du dessous (algorithme : [PhaseCorrelationMatcherClass](../algorithms/stitching/phase_correlation/PhaseCorrelation.md)). Le résultat est **toujours en pixels de pleine résolution**, même si l'analyse est faite sur un niveau réduit.

## Contrat

|               |                                                               |
| ------------- | ------------------------------------------------------------- |
| `name`        | `"phase-correlation"`                                         |
| entrée        | `{ "dataType": "Tiles", "key": "tiles" }`                     |
| sortie        | `{ "dataType": "PairMatches", "key": "matches" }`             |
| `getResult()` | `pairs`, `niveau`, `failures` (`{idA, idB, reason}`), `error` |

**Paramètres** : `overlap` (0.2), `niveau` (0 = pleine résolution, n = 1/2ⁿ, de 0 à 3), et les paramètres de la corrélation de phase : `lissage`, `seuilRapport`, `lissageLocal`, `ecartMax`, `espacementMin`, `marge`, `fractionValideMin`, `nbPoints`, `fenetre`, `facteurDft`. Ces noms n'ont pas changé avec la migration des algorithmes ; le champ de `PhaseCorrelationNs::Parameters` qui correspond à chacun est donné dans [PhaseCorrelation.md §5](../algorithms/stitching/phase_correlation/PhaseCorrelation.md#5-paramètres). Un paramètre absent du JSON prend sa valeur par défaut. Les longueurs en px sont en pixels **du niveau analysé** : à un niveau réduit, réduire `fenetre`, par exemple à `max(16, 64 / 2ⁿ)`, sinon aucun point local n'est trouvé.

## Conventions

- **Ordre** : pour chaque tuile A, sa voisine de droite puis celle du dessous (B). `direction` = position de B par rapport à A (`Right` ou `Bottom`).
- **Axes** : repère de A, origine au centre du pixel haut-gauche, x vers la droite, y vers le bas.
- **Signe** : `(translationX, translationY)` = coin haut-gauche de B dans A ; `pointsInA[i] − pointsInB[i] ≈ (translationX, translationY)`.
  - `Right` : `translationX > 0`, `translationY < 0` si B est plus haute que A.
  - `Bottom` : `translationY > 0`, `translationX < 0` si B est décalée vers la gauche.

## Résolution réduite

Au niveau n (s = 2ⁿ) : translation × s ; point `p ↦ (p + 0,5)·s − 0,5`.

| Niveau | Tolérance sur la translation (px pleine résolution) |
| ------ | --------------------------------------------------- |
| 0      | ±0,05                                               |
| n ≥ 1  | ±0,25·2ⁿ                                            |

La perte de précision aux niveaux réduits vient de la pyramide (moyenne 2 × 2), pas de la conversion.

## Échecs

Une paire non mesurable est ignorée et listée dans `failures`. Le step renvoie `false` avec `error` si aucune paire ne réussit, si un paramètre est invalide, s'il n'y a pas de tuiles ou si un fichier est illisible.

## Lecture des pixels

Seul accès aux pixels : `readPixels(tile, level)`. Elle lit aujourd'hui le fichier source, réduit comme le TIFF pyramidal (`createPyramidalTiledTiff`). Elle sera remplacée par `readRegion(tile, level, rectangle)` (Mathias B.) dès que cette fonction existe.
