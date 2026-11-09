# IntensityCorrectionStep

`StepsNs::IntensityCorrectionStepClass`, enregistré sous `intensity-correction`, estime la luminosité relative des tuiles dans leurs zones de recouvrement. Il réutilise `BrightnessEqualizationNs::BrightnessEqualizerClass` et ne modifie aucun pixel des sources. Les gains et le champ d'éclairage sont appliqués ensuite, à l'assemblage ou par `dynamic-programming-dijkstra`.

Code : `steps/intensity_correction_step.h` / `.cpp`. Version de la PR #72 (Amory707), qui remplace celle de la PR #80.

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | `tiles` | `Tiles` | `DataNs::TileList` | `import` |
| Entrée | `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` | `phase-correlation`, `sift` ou `lightglue` |
| Sortie | `intensityCorrection` | `IntensityCorrection` | `DataNs::IntensityCorrection` | `dynamic-programming-dijkstra` ; l'assemblage devra aussi l'appliquer |

- **Lit** dans `tiles` l'ordre des tuiles, leurs dimensions en pleine résolution et le chemin de chaque image source, lue une à la fois (voir « Lecture et mémoire »).
- **Lit** dans `matches` les identifiants `tileIdA` et `tileIdB` et la translation de chaque paire (`translationX`, `translationY`), qui situent la zone de recouvrement. Les points et les scores ne servent pas.
- **Écrit** `intensityCorrection`. Le type est déclaré dans `core/data/algorithm_types.h` et contient :
  - `tileGains` : un gain par tuile, dans le même ordre que `tiles` ;
  - `fieldStrength` : force `c` du champ d'éclairage commun, 0 avec la méthode `gain` ;
  - `isolatedTileIndices` : indices des tuiles sans recouvrement exploitable.

`getOutput()` renvoie `IntensityCorrectionOutput`, dont le champ `correction` contient la même correction. `getResult()` contient `method`, `tileCount`, `pairCount` et `isolatedTiles`, ou `error` en cas d'échec.

Les translations de `matches` doivent être en pixels de pleine résolution, dans le repère de la tuile A entière. Le step ne relance aucun matcher et ne recalcule pas les positions. Les aberrants doivent être rejetés en amont.

## Paramètres

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `method` | string | `gain-and-field` | `gain` : un gain par tuile ; `gain-and-field` : gains et champ commun |
| `blurSigma` | float | 1.5 | flou avant estimation, en pixels réduits |
| `borderMargin` | int | 40 | marge exclue aux bords, en pixels pleine résolution |
| `lowThreshold` | float | 0.03 | luminance minimale retenue |
| `highThreshold` | float | 0.97 | luminance maximale retenue |
| `iterationCount` | int | 6 | itérations de moindres carrés robustes |
| `huberThreshold` | float | 1.5 | seuil de Huber |
| `gaugeWeight` | float | 1000 | poids de la jauge des gains |

`StepClass::importConfig()` contrôle les noms et les types. Le step refuse une `method` inconnue ; l'algorithme valide le domaine des autres paramètres.

La réduction n'est pas un paramètre : elle suit le niveau de pyramide lu, le niveau 3 (1/8), et le step l'impose à l'algorithme (`reductionFactor = 8`).

## Convention des gains

`tileGains[i]` représente la **luminosité relative** de la tuile. Pour corriger une tuile, il faut **diviser** les pixels par son gain, et non les multiplier. Par exemple, si B est 25 % plus lumineuse que A, le rapport `gainB / gainA` vaut environ 1.25.

La moyenne géométrique des gains est centrée sur 1 par la jauge du modèle. Pour la méthode `gain-and-field`, on divise aussi par `exp(fieldStrength * r²)`, avec le rayon normalisé défini par l'algorithme.

Les tuiles isolées gardent un gain de 1. Le consommateur doit les laisser non corrigées, y compris pour le champ d'éclairage : `isolatedTileIndices` sert à les identifier.

## Lecture et mémoire

Chaque tuile est lue par `PhaseCorrelationStepClass::readPixels(tile, 3)`. Cette fonction relit **l'image source entière** (`cv::imread`), puis la réduit au niveau 3 comme le TIFF pyramidal. `BrightnessEqualizerClass::reduceTile()` en tire la luminance réduite. Une seule image pleine résolution est en mémoire à la fois ; seules les luminances réduites de toutes les tuiles sont gardées.

Le step dépend donc de `PhaseCorrelationStepClass` pour la lecture. Lire directement le niveau 3 du TIFF pyramidal avec `IONs::PyramidalTiffReaderClass::readRegion` ([PyramidalTiff.md](../core/PyramidalTiff.md)) éviterait de charger l'image pleine résolution. C'est une tâche à faire.

## Erreurs

`execute()` renvoie `false`, avec la cause dans `error`, si :

- un paramètre a un mauvais type ou si `method` est inconnue ;
- `tiles` ou `matches` manque dans le DataStore ;
- une image est illisible ;
- une paire de `matches` désigne une tuile inconnue : **tout le step échoue**, la paire n'est pas simplement ignorée ;
- l'estimation est impossible (moins de 2 tuiles, aucune paire exploitable, paramètre hors domaine).

En cas d'échec, le step ne touche pas au DataStore. Une `intensityCorrection` écrite par une exécution précédente y reste.

## Checkpoint (`intensityCorrection.json`)

Le type `IntensityCorrection` est sérialisé avec les autres types du pipeline, dans `pipelineTypes()` (`core/checkpoint/checkpoint_types.cpp`) :

```json
{ "tileGains": [0.98, 1.02, 1.01, 0.99], "fieldStrength": -0.04, "isolatedTileIndices": [] }
```

Les valeurs de cet exemple sont inventées. Le chargement relit les trois champs sans contrôler les valeurs.

## Exemple JSON

À placer après un step qui produit `matches` :

```json
{
  "name": "intensity-correction",
  "parameters": [
    { "valueType": "string", "name": "method", "value": "gain" }
  ]
}
```

Les autres paramètres gardent leur valeur par défaut.

## Tests

`tests/intensity_correction/test_intensity_correction.cpp`, 8 tests (`IntensityCorrectionStepTest.*`) :

- l'enregistrement sous son nom ;
- un gain connu retrouvé ;
- des gains centrés sur 1 ;
- les refus : méthode inconnue, `matches` absent, identifiant de tuile inconnu, paramètre de l'algorithme invalide ;
- l'enchaînement avec `dynamic-programming-dijkstra`.

La sauvegarde et la relecture du type `IntensityCorrection` sont vérifiées par `tests/core/test_reprise.cpp` (`Reprise.TypesDuPipeline`), sans passer par le step.

```sh
ctest --preset windows-aqt -R IntensityCorrectionStepTest
```
