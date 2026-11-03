# IntensityCorrectionStepClass

Code : `steps/intensity_correction_step.h` / `.cpp` (namespace `StepsNs`).

Estime la correction de luminosité de chaque tuile à partir des recouvrements mesurés par `phase-correlation` (algorithme : [Egalisation](../algorithms/egalisation/Egalisation.md)). **Le step ne modifie aucun pixel** : il produit des gains, que l'assemblage appliquera.

## Contrat

| | |
|---|---|
| `name` | `"intensity-correction"` |
| entrées | `{ "dataType": "Tiles", "key": "tiles" }` et `{ "dataType": "PairMatches", "key": "matches" }` |
| sortie | `{ "dataType": "IntensityCorrection", "key": "intensityCorrection" }` |
| `getResult()` | `method`, `tileCount`, `pairCount`, `isolatedTiles`, `error` |

**Paramètres** (un paramètre absent du JSON prend sa valeur par défaut) :

| Paramètre | Type | Défaut | Rôle |
|---|---|---|---|
| `method` | string | `"gain-and-field"` | `"gain"` : un gain par tuile ; `"gain-and-field"` : gains et champ d'éclairage commun |
| `blurSigma` | float | 1.5 | flou (px de l'image réduite) avant l'estimation |
| `borderMargin` | int | 40 | px ignorés au bord de chaque tuile (pleine résolution) |
| `lowThreshold` | float | 0.03 | luminance en dessous de laquelle un pixel est exclu |
| `highThreshold` | float | 0.97 | luminance au-dessus de laquelle un pixel est exclu (saturation) |
| `iterationCount` | int | 6 | itérations des moindres carrés repondérés |
| `huberThreshold` | float | 1.5 | seuil de Huber, en écarts-types robustes |
| `gaugeWeight` | float | 1000 | poids de la contrainte « moyenne des log des gains = 0 » |

La réduction des images (1/8, niveau 3 de la pyramide) est fixée par le step, pas par un paramètre.

## Conventions

- **Gain** : `tileGains[i]` est la luminosité relative de la tuile `i`. La correction **divise** la tuile par son gain. Moyenne géométrique des gains = 1.
- **Ordre** : `tileGains` suit l'ordre de la `TileList`.
- **Champ** : `fieldStrength` est le `c` de `exp(c · r²)`, commun à toutes les tuiles ; 0 avec la méthode `"gain"`.
- **Tuiles isolées** : `isolatedTileIndices` liste les tuiles sans paire exploitable (gain 1, non égalisées).
- **Translations** : celles de `matches`, en pleine résolution, dans le repère de A.

## Échecs

Le step renvoie `false` avec la cause dans `error` si une entrée manque, si `method` est inconnue, si un paramètre est invalide, si une image est illisible, si un identifiant de tuile des `matches` est inconnu, ou si l'estimation est impossible (moins de 2 tuiles, aucune paire avec des échantillons).

## Lecture des pixels

Comme `phase-correlation`, le step passe par `PhaseCorrelationStepClass::readPixels(tile, level)` au niveau 3. Une seule image pleine résolution est en mémoire à la fois. Quand `readRegion` existera, ce sera le seul endroit à changer.