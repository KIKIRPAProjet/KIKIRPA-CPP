# ExportStepClass

Code : [export_step.h](../../include/backend/steps/export_step.h) / [export_step.cpp](../../src/backend/steps/export_step.cpp), namespace `StepsNs`.

## Rôle

Dernier step du pipeline. Il écrit uniquement l'image composite produite par l'assemblage (`fusion`) dans un fichier TIFF tuilé (`mosaic.tif`). Il ne crée ni fichier JSON, ni fichier de provenance, ni masque séparé.

## Contrat

| Élément | Valeur |
|---|---|
| Nom du step | `export` |
| Paramètre | `outputDirectory` (`string`), vide par défaut : dossier Téléchargements de l'utilisateur courant |
| Entrée `compositeBlocks` | Type logique `CompositeBlocks`, type C++ `DataNs::CompositeBlockList` (fourni par `fusion`) |
| Sorties DataStore | Aucune : ce step termine le pipeline |

`CompositeBlock` est défini dans `core/data/algorithm_types.h`. L'export lit dans chaque bloc uniquement :

- `imageRgb` : `CV_16UC3`, trois canaux couleur ;
- `blockRect` : `cv::Rect` donnant la position du bloc sur la mosaïque.

Les autres champs du bloc (`provenance` et `mask`) ne sont pas exportés et ne sont pas requis par ce step. Ils peuvent être conservés par le contrat partagé pour les traitements internes ou futurs.

Les rectangles doivent former une grille complète alignée sur `DataNs::BLOCK_SIZE` (512 pixels). Les blocs de bord peuvent être plus petits. L'assemblage doit fournir une liste qui couvre toute la grille de sortie.

## Fichier produit

Le seul fichier créé ou remplacé par le step est `mosaic.tif`, une image couleur 16 bits à trois canaux. Le TIFF est tuilé en blocs de 512 × 512 pixels, pleine résolution uniquement et compressé en LZW. Le TIFF classique ou BigTIFF est choisi selon une estimation conservatrice de la taille.

Le fichier est d'abord écrit sous un nom temporaire, puis publié en remplacement du fichier de destination afin qu'une erreur d'écriture ne détruise pas la mosaïque existante. Les éventuels fichiers annexes provenant d'anciennes exécutions ne sont pas supprimés automatiquement.

`getOutput()` fournit le chemin final, les dimensions inférées de la mosaïque, le nombre de blocs et l'indication TIFF/BigTIFF. `getResult()` résume ces informations pour le journal du pipeline.

## Échecs et reprise

Le step échoue avec `result["error"]` si l'entrée manque, si les blocs sont vides, mal typés, mal dimensionnés, dupliqués ou si la grille est incomplète, ou si la création/l'écriture du fichier échoue. Le répertoire de sortie est créé s'il n'existe pas.

L'empreinte de reprise suit l'existence, la taille et la date de modification de `mosaic.tif`. Si le fichier est supprimé ou modifié, le checkpoint de ce step est considéré périmé.

Le checkpoint ne sérialise pas actuellement `CompositeBlockList`. Un producteur qui déclare cette sortie aura un checkpoint incomplet et sera rejoué à la reprise. Ajouter une sérialisation des blocs serait coûteux et doit être décidé avec le contrat de Fusion, plutôt que d'écrire implicitement les matrices dans le JSON du checkpoint.

## État de l'enchaînement

Le registre expose `export`, alimenté directement par le step `fusion` qui produit `compositeBlocks`.

## Test ciblé

La cible `export_step_tests` est définie dans `tests/core/CMakeLists.txt`. Après configuration du preset du système et compilation, CTest permet de lancer uniquement ces tests :

```text
cmake --build --preset windows-aqt --target export_step_tests
ctest --preset windows-aqt -R ExportStepTest
```

Remplacer `windows-aqt` par `linux` ou `macos` selon le système.
