# PhaseCorrelationStepClass

Code : `steps/phase_correlation_step.h` / `.cpp` (namespace `StepsNs`).

Mesure par corrélation de phase la position de chaque tuile par rapport à ses voisines de droite et du dessous (algorithme : [PhaseCorrelationMatcherClass](../algorithms/stitching/phase_correlation/PhaseCorrelation.md)). Le résultat est **toujours en pixels de pleine résolution**, même si l'analyse est faite sur un niveau réduit.

## Contrat

|               |                                                                                                                      |
| ------------- | -------------------------------------------------------------------------------------------------------------------- |
| `name`        | `"phase-correlation"`                                                                                                |
| entrée        | `{ "dataType": "Tiles", "key": "tiles" }`                                                                            |
| sortie        | `{ "dataType": "PairMatches", "key": "matches" }`                                                                    |
| `getResult()` | `pairs`, `niveau`, `failures` (`{idA, idB, reason}`), `confidence` (une entrée par paire, voir § Confiance), `error` |

**Paramètres** : `overlap` (0.2), `niveau` (0 = pleine résolution, n = 1/2ⁿ, de 0 à 3), et les paramètres de la corrélation de phase : `lissage`, `seuilRapport`, `lissageLocal`, `ecartMax`, `espacementMin`, `marge`, `fractionValideMin`, `nbPoints`, `fenetre`, `facteurDft`, et les contrôles de confiance (§ Confiance). Ces noms n'ont pas changé avec la migration des algorithmes ; le champ de `PhaseCorrelationNs::Parameters` qui correspond à chacun est donné dans [PhaseCorrelation.md §5](../algorithms/stitching/phase_correlation/PhaseCorrelation.md#5-paramètres). Un paramètre absent du JSON prend sa valeur par défaut. Les longueurs en px sont en pixels **du niveau analysé** : à un niveau réduit, réduire `fenetre`, par exemple à `max(16, 64 / 2ⁿ)`, sinon aucun point local n'est trouvé.

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

## Confiance

Chaque paire mesurée passe les contrôles actifs : NCC et auto-similarité par défaut, la dispersion étant mesurée et exportée, mais pas utilisée pour rejeter. Une paire qui échoue à un contrôle est rejetée (dans `failures`, raison commençant par `confiance :`). Seuils et justification : [EtudeConfiance](../algorithms/stitching/phase_correlation/EtudeConfiance.md).

| Option | Défaut | Rejette si | Détecte | Désactiver |
|---|---|---|---|---|
| `nccMin` | 0.3 | NCC médiane des points < seuil | fond sombre, bruit | `0` |
| `dispersionMax` | 0 (désactivé) | écart médian points / translation > seuil (px du niveau analysé) | fond sombre sur une partie de la bande | `0` |
| `autosimMax` | 0.6 | auto-similarité de la bande de A ≥ seuil | motif répété (mesure ambiguë) | `1` |

Sans point local, deux cas :
- la zone commune est **assez grande** pour un patch, mais aucun point n'a été jugé fiable : la paire est **rejetée** (bruit) ;
- elle est **trop petite** pour un patch (moins de `2·fenetre + 2·⌈ecartMax⌉ + 2` px), ou `nbPoints = 0` : la paire est **gardée** avec l'état `non_verifie`.

### Indicateurs exportés

Chaque paire, gardée ou rejetée, a une entrée `PairConfidence` dans `getOutput().confidence` et dans `getResult()["confidence"]` :

```json
{ "idA": 0, "idB": 1, "dir": "Right", "etat": "fiable",
  "ncc": 0.999, "dispersion": 0.01, "autosim": 0.075,
  "points": [ { "a": [863.0, 798.0], "b": [63.01, 798.0], "ncc": 1.0 }, ... ] }
```

| Champ | Contenu |
|---|---|
| `etat` | `fiable`, `non_verifie` (gardées) ; `ambigu`, `rejete` (rejetées, raison dans `failures`) |
| `ncc` | Score de la paire : NCC médiane des points (0 à 1). `null` sans point |
| `dispersion` | Écart médian (px pleine résolution) entre `a − b` et la translation. `null` sans point |
| `autosim` | Auto-similarité de la bande de A (0 à 1). `null` si la bande est trop vide ou trop étroite |
| `points` | Paires de points `a` (tuile A) ↔ `b` (tuile B), px pleine résolution, avec la NCC de chacune |

Pour une paire que l'algorithme n'a pas pu mesurer, tous les indicateurs sont `null` et `points` est vide.

## Échecs

Une paire non mesurable, ou rejetée par un contrôle de confiance, est ignorée et listée dans `failures`. Le step renvoie `false` avec `error` si aucune paire ne réussit, si un paramètre est invalide, s'il n'y a pas de tuiles ou si un fichier est illisible.

## Lecture des pixels

Seul accès aux pixels : `readPixels(tile, level)`. Elle lit aujourd'hui le fichier source, réduit comme le TIFF pyramidal (`createPyramidalTiledTiff`). Elle sera remplacée par `readRegion(tile, level, rectangle)` (Mathias B.) dès que cette fonction existe.
