# PhaseCorrelationStep

Mesure par corrélation de phase la position de chaque tuile par rapport à ses voisines de droite et du dessous (algorithme : [CorrelationPhase](../algorithms/stitching/correlation_phase/CorrelationPhase.md)). Le résultat est **toujours en pixels de pleine résolution**, même si l'analyse est faite sur un niveau réduit.

## Contrat

| | |
|---|---|
| `name` | `"phase-correlation"` |
| entrée | `{ "type": "Tiles", "key": "tiles" }` |
| sortie | `{ "type": "PairMatches", "key": "matches" }` |
| `exportData()` | `pairs`, `niveau`, `failures` (`{idA, idB, reason}`), `unverified` (`{idA, idB}`), `error` |

**Options** : `overlap` (0.2), `niveau` (0 = pleine résolution, n = 1/2ⁿ, de 0 à 3), et les paramètres de `CorrelationPhase` : `lissage`, `seuilRapport`, `lissageLocal`, `ecartMax`, `espacementMin`, `marge`, `fractionValideMin`, `nbPoints`, `fenetre`, `facteurDft`, et les contrôles de confiance (§ Confiance). Une option absente du JSON prend sa valeur par défaut. Les longueurs en px sont en pixels **du niveau analysé** : à un niveau réduit, réduire `fenetre`, par exemple à `max(16, 64 / 2ⁿ)`, sinon aucun point local n'est trouvé.

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

## Confiance

Chaque paire mesurée passe 3 contrôles. Une paire qui en échoue un est rejetée (dans `failures`, raison commençant par `confiance :`). Seuils et justification : [EtudeConfiance](../algorithms/stitching/correlation_phase/EtudeConfiance.md).

| Option | Défaut | Rejette si | Détecte | Désactiver |
|---|---|---|---|---|
| `nccMin` | 0.3 | NCC médiane des points < seuil | fond sombre, bruit | `0` |
| `dispersionMax` | 0.5 | écart médian points / translation > seuil (px du niveau analysé) | fond sombre sur une partie de la bande | `0` |
| `autosimMax` | 0.6 | auto-similarité de la bande de A ≥ seuil | motif répété (mesure ambiguë) | `1` |

Sans point local, deux cas :
- la zone commune est **assez grande** pour un patch, mais aucun point n'a été jugé fiable : la paire est **rejetée** (bruit) ;
- elle est **trop petite** pour un patch (moins de `2·fenetre + 2·⌈ecartMax⌉ + 2` px), ou `nbPoints = 0` : la paire est **gardée** mais listée dans `unverified`.

## Échecs

Une paire non mesurable, ou rejetée par un contrôle de confiance, est ignorée et listée dans `failures`. Le step renvoie `false` avec `error` si aucune paire ne réussit, si une option est invalide, s'il n'y a pas de tuiles ou si un fichier est illisible.

## Lecture des pixels

Seul accès aux pixels : `lirePixels(tuile, niveau)`. Elle lit aujourd'hui le fichier source, réduit comme `PyramidalTiffWriter`. Elle sera remplacée par `readRegion(tuile, niveau, rect)` (Mathias B.) dès que cette fonction existe.
