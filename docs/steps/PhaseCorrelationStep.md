# PhaseCorrelationStep

Mesure par corrélation de phase la position de chaque tuile par rapport à ses voisines de droite et du dessous (algorithme : [CorrelationPhase](../algorithms/stitching/correlation_phase/CorrelationPhase.md)). Le résultat est **toujours en pixels de pleine résolution**, même si l'analyse est faite sur un niveau réduit.

## Contrat

| | |
|---|---|
| `name` | `"phase-correlation"` |
| entrée | `{ "type": "Tiles", "key": "tiles" }` |
| sortie | `{ "type": "PairMatches", "key": "matches" }` |
| `exportData()` | `pairs`, `niveau`, `failures` (`{idA, idB, reason}`), `error` |

**Options** : `overlap` (0.2), `niveau` (0 = pleine résolution, n = 1/2ⁿ, de 0 à 3), et les paramètres de `CorrelationPhase` : `lissage`, `seuilRapport`, `lissageLocal`, `ecartMax`, `espacementMin`, `marge`, `fractionValideMin`, `nbPoints`, `fenetre`, `facteurDft`. Une option absente du JSON prend sa valeur par défaut. Les longueurs en px sont en pixels **du niveau analysé** : à un niveau réduit, réduire `fenetre`, par exemple à `max(16, 64 / 2ⁿ)`, sinon aucun point local n'est trouvé.

## Conventions

- **Ordre** : pour chaque tuile A, sa voisine de droite puis celle du dessous (B). `dir` = position de B par rapport à A (`Right` ou `Bottom`).
- **Axes** : repère de A, origine au centre du pixel haut-gauche, x vers la droite, y vers le bas.
- **Signe** : `(dx, dy)` = coin haut-gauche de B dans A ; `pointsA[i] − pointsB[i] ≈ (dx, dy)`.
  - `Right` : `dx > 0`, `dy < 0` si B est plus haute que A.
  - `Bottom` : `dy > 0`, `dx < 0` si B est décalée vers la gauche.

## Résolution réduite

Au niveau n (s = 2ⁿ) : translation × s ; point `p ↦ (p + 0,5)·s − 0,5`.

| Niveau | Tolérance sur dx et dy (px pleine résolution) |
|---|---|
| 0 | ±0,05 |
| n ≥ 1 | ±0,25·2ⁿ |

La perte de précision aux niveaux réduits vient de la pyramide (moyenne 2 × 2), pas de la conversion.

## Échecs

Une paire non mesurable est ignorée et listée dans `failures`. Le step renvoie `false` avec `error` si aucune paire ne réussit, si une option est invalide, s'il n'y a pas de tuiles ou si un fichier est illisible.

## Lecture des pixels

Seul accès aux pixels : `lirePixels(tuile, niveau)`. Elle lit aujourd'hui le fichier source, réduit comme `PyramidalTiffWriter`. Elle sera remplacée par `readRegion(tuile, niveau, rect)` (Mathias B.) dès que cette fonction existe.
