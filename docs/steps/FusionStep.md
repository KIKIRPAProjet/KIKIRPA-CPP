# FusionStepClass

Code : [fusion_step.h](../../include/backend/steps/fusion_step.h) / [fusion_step.cpp](../../src/backend/steps/fusion_step.cpp), namespace `StepsNs`.

## Rôle

Avant-dernier step du pipeline. Il assemble les tuiles de l'image en blocs composites tuilés en appliquant les masques de découpe des territoires (`graphcut`), les positions absolues des tuiles (`absolute-pos`), et le cas échéant les gains de luminosité (`intensity-correction`).

## Contrat

| Élément | Valeur |
|---|---|
| Nom du step | `fusion` |
| Paramètres | `blockSize` (`int`, 512 par défaut), `outputPath` (`string`, vide par défaut), `enableBlending` (`bool`, `false`) |
| Entrées | `tiles` : `Tiles`, `positions` : `Positions`, `territories` : `Territories` |
| Entrée optionnelle | `intensityCorrection` : `IntensityCorrection` (appliquée si présente) |
| Sortie DataStore | `compositeBlocks` : `CompositeBlocks` (`DataNs::CompositeBlockList`) |

Chaque bloc composite (`DataNs::CompositeBlock`) contient :
- `imageRgb` : les pixels assemblés en `CV_16UC3` ;
- `provenance` : l'identifiant de la tuile source (`CV_16UC1`) ;
- `mask` : le masque des pixels valides (`CV_8UC1`) ;
- `blockRect` : le rectangle du bloc dans le repère global de la mosaïque.
