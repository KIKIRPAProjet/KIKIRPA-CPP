# ExportStepClass

Code : [export_step.h](../../include/backend/steps/export_step.h) / [export_step.cpp](../../src/backend/steps/export_step.cpp), namespace `StepsNs`.

## Rôle

Dernier step du pipeline. Il écrit l'image composite produite par l'assemblage (`fusion`) dans un fichier TIFF tuilé (`mosaic.tif`).

## Contrat

| Élément | Valeur |
|---|---|
| Nom du step | `export` |
| Paramètre | `outputDirectory` (`string`), vide par défaut : dossier Téléchargements ou dossier local `output` |
| Entrée `compositeBlocks` | Type logique `CompositeBlocks`, type C++ `DataNs::CompositeBlockList` (fourni par `fusion`) |
| Sorties DataStore | Aucune : ce step termine le pipeline |

`CompositeBlock` est défini dans `core/data/algorithm_types.h`. L'export lit dans chaque bloc :
- `imageRgb` : `CV_16UC3`, trois canaux couleur ;
- `blockRect` : `cv::Rect` donnant la position du bloc sur la mosaïque.

Les rectangles forment une grille complète alignée sur `DataNs::BLOCK_SIZE` (512 pixels).

## Fichier produit

Le fichier créé est `mosaic.tif`, une image couleur 16 bits à trois canaux tuilée en blocs de 512 × 512 pixels compressée en LZW (TIFF classique ou BigTIFF selon la taille).
