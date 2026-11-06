# IntensityCorrectionStep

`StepsNs::IntensityCorrectionStepClass`, enregistré sous `intensity-correction`, estime la luminosité relative des tuiles dans leurs zones de recouvrement. Il réutilise `BrightnessEqualizationNs::BrightnessEqualizerClass` et ne modifie aucun pixel des sources. Les gains et le champ d'éclairage sont appliqués ensuite, lors de l'assemblage.

## Contrat

| Sens | Clé | Type JSON | Type C++ |
|---|---|---|---|
| Entrée | `tiles` | `Tiles` | `DataNs::TileList` |
| Entrée | `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` |
| Sortie | `intensityCorrection` | `IntensityCorrection` | `DataNs::IntensityCorrection` |

Le type partagé est déclaré dans `core/data/intensity_correction.h`. Il contient :

- `tileGains` : un gain par tuile, dans le même ordre que `TileList` ;
- `fieldStrength` : force `c` du champ d'éclairage commun ;
- `isolatedTileIndices` : indices des tuiles sans recouvrement exploitable.

`IntensityCorrectionOutput` possède les champs `key`, `value`, `type`. Le résultat détaillé est accessible par `getOutput()` et dans le DataStore. `getResult()` garde uniquement un résumé : `pairs`, `samples`, `tileCount`, `method`, `isolatedTiles`, `failures`, `error` (les compteurs détaillés sont présents après une estimation réussie).

Les translations de `matches` sont en pixels pleine résolution et dans le repère de chaque tuile complète. L'étape ne relance aucun matcher et ne recalcule pas les positions. Elle peut consommer les sorties de la corrélation de phase, de SIFT ou de LightGlue, sous réserve de ce contrat commun. Les aberrants doivent être rejetés en amont.

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

`StepClass::importConfig()` contrôle les clés et les types et conserve les valeurs par défaut des paramètres absents. Le step vérifie la finitude des paramètres et les bornes des entiers avant conversion ; l'algorithme valide leur domaine numérique.

La réduction est fixée à 1/8 pour l'estimation et ne constitue pas une option JSON. Le champ `algorithmParameters.reductionFactor` de l'entrée C++ n'est donc pas utilisé pour modifier ce facteur.

## Convention des gains

`tileGains[i]` représente la **luminosité relative** de la tuile. Pour corriger une tuile, il faut **diviser** les pixels par son gain, et non les multiplier. Par exemple, si B est 25 % plus lumineuse que A, le rapport `gainB / gainA` doit valoir environ 1.25.

La moyenne géométrique des gains des tuiles participantes est centrée sur 1 par la jauge du modèle. Pour la méthode `gain-and-field`, on divise également par `exp(fieldStrength * r²)` ; le rayon normalisé est celui défini par l'algorithme existant. La méthode `gain` produit `fieldStrength = 0`.

Les tuiles isolées gardent un gain de 1. Le consommateur doit les laisser non corrigées, y compris pour le champ d'éclairage global : `isolatedTileIndices` sert à les identifier.

## Lecture et mémoire

Les images sont lues depuis `TileClass::getSource()` avec `IMREAD_UNCHANGED`. Le step vérifie leurs dimensions et leur type par rapport aux métadonnées d'import. Une seule image pleine résolution est chargée à la fois ; seules les luminances réduites de toutes les tuiles sont conservées pour l'estimation.

La réduction utilise directement `BrightnessEqualizerClass::reduceTile()`, qui préserve la dynamique des images 8/16 bits. Le step ne dépend pas des fonctions internes de `PhaseCorrelationStepClass`. Il ne lit pas encore les régions TIFF ni un niveau réduit directement sur disque ; cette optimisation pourra utiliser une API de lecture commune lorsqu'elle sera disponible sur main.

## Erreurs et résultats partiels

Les paires invalides sont ignorées et listées dans `failures` sous la forme `{idA, idB, reason}` : identifiant inconnu, paire d'une tuile avec elle-même, translation non finie, doublon ou absence d'échantillons valides. Les doublons, y compris une paire renversée, ne sont pas comptés une seconde fois.

Une estimation peut réussir avec certaines paires ignorées et des tuiles isolées. Elle échoue si aucune paire n'est exploitable. Elle échoue aussi si les entrées manquent, si une liste contient une tuile nulle ou des identifiants dupliqués, si moins de deux tuiles sont fournies, si une image est illisible, si les métadonnées ne correspondent plus aux pixels ou si les paramètres sont invalides.

Les exceptions sont converties en `false` et en message d'erreur. Au début de chaque exécution, les anciennes sorties sont effacées dans l'objet et dans le DataStore. Aucune ancienne correction ne reste publiée après un échec.

## Checkpoints

La construction du step enregistre le type `IntensityCorrection` via l'API `CheckpointClass::registerType()`, si ce type n'a pas déjà été déclaré. L'enregistrement respecte donc un éventuel sérialiseur personnalisé existant. Il se fait avant l'exécution ou la reprise d'un pipeline.

La sérialisation, isolée dans `core/checkpoint/intensity_correction_checkpoint.cpp`, sauvegarde exactement les gains, le champ et les indices isolés. Elle contrôle les gains positifs et finis, la finitude du champ et les bornes des indices isolés. Le chargement ne modifie le DataStore qu'après lecture et validation du document complet.

## Exemple JSON

À placer après un step produisant `matches` :

```json
{
  "name": "intensity-correction",
  "parameters": [
    { "valueType": "string", "name": "method", "value": "gain" }
  ]
}
```

Les autres paramètres gardent leur valeur par défaut. Le registre déclare les entrées et sorties ; `buildPipeline()` vérifie l'enchaînement et les types.

## Tests

Les tests reprennent les cas initiaux et les complètent : gain connu sur de vraies tuiles 16 bits, jauge des gains, source 8 bits, intégration import → phase-correlation → intensity-correction, types et paramètres invalides, remise à zéro après échec, paires ignorées, recouvrement vide, tuile isolée, images absentes, métadonnées incohérentes, préservation des sources et sauvegarde/restauration par checkpoint.

```sh
ctest --test-dir build --output-on-failure -R IntensityCorrectionStepTest
```

Cette branche réunit les deux versions du step : celle d'Amory707 (`fe/intensity-correction-step`, PR #72), dont l'historique est conservé, et les vérifications et tests ajoutés par Mghiiii (PR #80). L'issue #19 ne fournit pas de description supplémentaire : le contrat existant est donc maintenu.
