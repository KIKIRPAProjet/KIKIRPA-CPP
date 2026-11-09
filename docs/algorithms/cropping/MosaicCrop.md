# Rognage d'une mosaïque par blocs

## Rôle

`CroppingNs::cropMosaic` conserve une zone rectangulaire d'une mosaïque
produite par l'assemblage.

Le résultat utilise le même format de fichiers que l'assemblage :
des blocs PNG couleur, provenance et masque, dont les positions sont
indiquées dans les noms.

Cet algorithme est indépendant de Qt et du pipeline.
Il ne contient aucun step et ne produit pas de TIFF final.

## Fichiers du code

- Interface : `include/backend/algorithms/cropping/mosaic_crop.h`
- Implémentation : `src/backend/algorithms/cropping/mosaic_crop.cpp`
- Type partagé : `include/backend/core/data/mosaic_files.h`
- Tests : `tests/cropping/test_mosaic_crop.cpp`

## Entrées

La structure `CroppingNs::CropInput` contient :

| Champ | Type | Description |
|---|---|---|
| `mosaic` | `DataNs::MosaicFiles` | Dossier source et nombre de blocs |
| `rectangle` | `cv::Rect` | Zone à conserver, en pixels pleine résolution |
| `outputDirectory` | `std::filesystem::path` | Nouveau dossier de sortie |

`DataNs::MosaicFiles` contient :

- `mosaicPath` : chemin du dossier des blocs ;
- `totalBlocks` : nombre de blocs, chaque bloc comprenant trois fichiers.

### Format des blocs sources

Pour chaque position `(X, Y)`, le dossier contient :

| Fichier | Type OpenCV | Contenu |
|---|---|---|
| `rgb_x<X>_y<Y>.png` | `CV_16UC3` | Pixels couleur |
| `provenance_x<X>_y<Y>.png` | `CV_16UC1` | Identifiants de provenance |
| `mask_x<X>_y<Y>.png` | `CV_8UC1` | Masque de validité |

Les trois fichiers d'un bloc doivent avoir les mêmes dimensions.
Les coordonnées peuvent être négatives.

### Repère du rectangle

Le rectangle utilise le même repère que les positions des fichiers sources.
Il ne s'agit pas des coordonnées d'un aperçu affiché à l'écran.

Les bords droit et inférieur sont exclus :
`[x, x + width)` et `[y, y + height)`.

Par exemple, `cv::Rect(100, 200, 800, 600)` conserve les pixels :

- de 100 à 899 horizontalement ;
- de 200 à 799 verticalement.

Le rectangle doit être non vide, entièrement couvert par les blocs sources
et sans chevauchement entre les blocs à l'intérieur de la zone conservée.

## Sortie

La fonction renseigne un `DataNs::MosaicFiles` :

- `mosaicPath` désigne le dossier du résultat ;
- `totalBlocks` donne le nombre de blocs conservés.

Le coin supérieur gauche du rectangle devient l'origine `(0, 0)`.

Les fichiers de sortie suivent la même convention de nommage que les sources.
Les blocs peuvent avoir des dimensions différentes et ne sont pas réalignés
sur une grille régulière.

Les dimensions de la mosaïque rognée correspondent à la largeur et à la
hauteur du rectangle demandé.

## Traitement

L’implémentation est organisée en trois modules internes :

- **préparation** : validation des entrées, lecture des blocs et sélection
  des intersections avec le rectangle demandé ;
- **vérification de couverture** : détection des chevauchements et validation
  de la couverture complète de la zone rognée ;
- **écriture** : création du dossier de sortie, copie des blocs complets,
  découpe des blocs partiels et nettoyage en cas d’échec.

Pour chaque bloc source :

1. Calculer son intersection avec le rectangle.
2. Exclure le bloc si l'intersection est vide.
3. Copier ses trois fichiers sans décodage s'il est entièrement conservé.
4. Sinon, découper les trois fichiers avec le même rectangle local.
5. Recalculer sa position par rapport à l'origine du rognage.

Les fichiers sources restent inchangés.
Aucun rééchantillonnage ni conversion de profondeur n'est effectué.

## Mémoire et lecture des fichiers

Les dimensions et les formats sont lus dans les en-têtes PNG de tous les blocs.

Seuls les fichiers des blocs partiellement conservés sont décodés avec
`cv::IMREAD_UNCHANGED`, un fichier à la fois.

Les blocs entièrement conservés sont copiés sans réencodage.
Leur contenu compressé n'est donc pas vérifié intégralement.

## Gestion des erreurs

La fonction renvoie :

- `true` en cas de succès, avec `output` renseigné et `errorMessage` vide ;
- `false` en cas d'échec, avec `output` vide et un message en anglais.

Les erreurs détectées comprennent notamment :

- paramètres invalides ;
- fichier absent ou en-tête PNG invalide ;
- profondeur, canaux ou dimensions inattendus ;
- nombre de blocs incorrect ;
- rectangle non couvert ou blocs qui se chevauchent dans la zone conservée ;
- échec de lecture, de décodage, de copie ou d'écriture.

Le dossier de sortie doit être inexistant et son parent doit exister.
Un résultat existant n'est jamais écrasé.

Si une erreur survient après la création du dossier de sortie,
la fonction tente de supprimer ce résultat incomplet.
Un échec de nettoyage est ajouté au message d'erreur.

## Exemple d'utilisation

```cpp
#include "algorithms/cropping/mosaic_crop.h"

#include <string>

CroppingNs::CropInput input;
input.mosaic = {"fusion_output", 12};
input.rectangle = cv::Rect(100, 200, 800, 600);
input.outputDirectory = "cropped_output";

DataNs::MosaicFiles output;
std::string errorMessage;

const bool success =
    CroppingNs::cropMosaic(input, output, errorMessage);
```

Cet exemple suppose que les 12 blocs sources couvrent le rectangle demandé.

La structure `FusionOutput` actuelle de l'assemblage et `MosaicFiles`
sont deux types C++ distincts avec les mêmes champs.
L'appelant peut transmettre les données ainsi :

```cpp
input.mosaic = {
    fusionOutput.mosaicPath,
    fusionOutput.totalBlocks
};
```

L'algorithme n'inclut pas le header du step d'assemblage.

## Tests

Les tests utilisent uniquement des images synthétiques.

Ils couvrent notamment :

- conservation de la mosaïque complète sans réencodage ;
- rognage à travers plusieurs blocs et à l'intérieur d'un seul bloc ;
- recalage des positions et coordonnées sources négatives ;
- conservation des pixels couleur, de provenance et de masque ;
- entrées invalides et fichiers manquants ;
- profondeurs et canaux PNG invalides ;
- dimensions incohérentes entre fichiers d’un même bloc ;
- signature PNG invalide ;
- limites des coordonnées entières ;
- protection d'un dossier de sortie existant ;
- parent de sortie absent ;
- alias où `output` désigne `input.mosaic`, en succès et en échec ;
- nettoyage après échec de décodage ;
- trous et chevauchements dans la zone conservée.

Depuis la racine du dépôt, sous Windows :

```bat
cmake --build --preset windows-aqt --target mosaic_crop_tests
ctest --preset windows-aqt -R MosaicCropTestClass --output-on-failure
```