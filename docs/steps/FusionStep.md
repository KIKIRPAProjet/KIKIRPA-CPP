# FusionStepClass

Code : [fusion_step.h](../../include/backend/steps/fusion_step.h) / [fusion_step.cpp](../../src/backend/steps/fusion_step.cpp), namespace `StepsNs`. S'appuie sur `IONs::TiledTiffWriterClass` ([core/TiledTiffWriter.md](../core/TiledTiffWriter.md)), `IONs::PyramidalTiffReaderClass` ([core/PyramidalTiff.md](../core/PyramidalTiff.md)) et `BrightnessEqualizationNs::OutputClass::correctRegion` ([BrightnessEqualization.md](../algorithms/brightness_equalization/BrightnessEqualization.md)).

**Assemblage** : fabrique l'image finale de la mosaïque, en pleine résolution, **bloc par bloc**. Chaque pixel est pris dans la tuile dont le territoire (calculé par `graphcut`) le contient, corrigé en intensité. Chaque bloc de 512 × 512 est écrit sur disque dès qu'il est calculé : la mosaïque entière n'est jamais en mémoire.

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | `tiles` | `Tiles` | `DataNs::TileList` | `import` |
| Entrée | `positions` | `Positions` | `std::vector<DataNs::TilePositionData>` | `absolute-pos` |
| Entrée | `territories` | `Territories` | `std::vector<DataNs::TileTerritory>` | `graphcut` |
| Entrée facultative | `intensityCorrection` | `IntensityCorrection` | `DataNs::IntensityCorrection` | `intensity-correction`, si placé avant |
| Sortie | `mosaic` | `Mosaic` | `DataNs::Mosaic` | `export` |

- **Lit** dans `tiles` le TIFF pyramidal de chaque tuile (`getTiff()`), niveau 0 seulement, **région par région** : pour chaque bloc, uniquement l'intersection tuile / bloc, et seulement si le territoire de la tuile touche cette intersection. Le nom du fichier source (`getSource()`) sert à retrouver la position ; l'image source n'est pas relue.
- **Lit** dans `positions` la position de chaque tuile, retrouvée par l'identifiant `rLL_cCC` que `GraphCutNs::ImagePretreatmentClass::parseTileName` tire du nom de fichier, comme `graphcut`. La rotation est ignorée, comme dans `graphcut`.
- **Lit** dans `territories` les polygones et les trous de chaque tuile (`tileIndex` = place dans `tiles`).
- **Lit**, si la clé existe, `intensityCorrection` : `tileGains`, `fieldStrength`, `isolatedTileIndices`. Elle n'est pas déclarée dans `inputs`, parce que le moteur n'a pas d'entrée facultative : la déclarer imposerait `intensity-correction` dans tout pipeline.
- **Écrit** dans `mosaic` les chemins de trois TIFF tuilés et `canvasSize` :

| Fichier | Type | Contenu |
|---|---|---|
| `mosaic_image.tif` | `CV_16UC3`, **RGB** | pixels corrigés |
| `mosaic_provenance.tif` | `CV_16UC1` | `TileClass::getId()` de la tuile d'où vient le pixel ; 0 là où le masque vaut 0 |
| `mosaic_mask.tif` | `CV_8UC1` | 255 = pixel fourni par une tuile, 0 = aucun territoire |

`getResult()` : `image` (chemin), `canvasSize`, `blockCount`, ou `error`.

## Paramètre

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `outputDirectory` | string | vide | dossier des trois TIFF, créé si besoin. Vide : `<dossier temporaire du système>/kikirpa-fusion`. Pour une vraie œuvre, choisir un disque avec de la place : l'image seule fait environ 6 octets par pixel avant compression |

## Fonctionnement

1. **Placement** : les coins des tuiles sont calculés **exactement comme `graphcut`** (`computeMosaicCorners`, même calcul que `ImagePretreatmentClass::processImages`, étape 3) : le minimum des positions, arrondi vers le bas, est soustrait, puis chaque coin est arrondi au pixel (`cvRound`). Les territoires sont dans ce repère ; arrondir avant de soustraire décalerait d'un pixel dans certains cas. La mosaïque commence en (0, 0) et mesure `max(coin + taille)` des tuiles.
2. **Grille** : des blocs de `DataNs::BLOCK_SIZE` (512) depuis (0, 0), ligne par ligne ; ceux des bords droit et bas sont plus petits.
3. **Un bloc** (`renderBlock`, fonction pure) : pour chaque tuile qui chevauche le bloc,
   - le territoire est dessiné sur l'intersection, polygone par polygone (`cv::drawContours`, avec la règle de `GraphCutSeamFinderClass::verifyTiling` : un trou est vidé mais son contour appartient au territoire) ;
   - s'il touche l'intersection, la région est lue dans le TIFF, divisée par `tileGains[i] · exp(fieldStrength · r²)` avec `OutputClass::correctRegion` (r² calculé dans le repère de la tuile entière ; tuile isolée : non corrigée), convertie de BGR en RGB, puis copiée sous le masque ;
   - provenance et masque sont remplis au même endroit.
4. **Écriture** : le bloc est écrit aussitôt dans les trois fichiers, puis oublié. Les fichiers sont sous un nom temporaire jusqu'à la fin ; en cas d'échec, les temporaires sont supprimés.

**Tuiles acceptées** : 8 ou 16 bits par canal, 1, 3 ou 4 canaux (le niveau de gris est recopié sur les trois canaux, l'alpha est abandonné). Les tuiles 8 bits sont multipliées par 257 (255 → 65535) : la mosaïque est toujours en 16 bits.

Ce travail reprend la logique de rendu par bloc et de masque de territoire (polygones avec trous) des PR #69 et #112.

## Erreurs

`execute()` renvoie `false`, avec la cause dans `result["error"]` (en anglais, préfixée par `fusion:`), si :

- une entrée manque ;
- il n'y a aucune tuile, ou pas autant de positions (ou de gains) que de tuiles ;
- un nom de fichier ne finit pas par `_rLL_cCC.tif`, ou sa position est absente ;
- une tuile n'a pas de TIFF pyramidal, ou son identifiant dépasse 65535 (provenance sur 16 bits) ;
- un territoire désigne une tuile inexistante, ou un gain n'est pas strictement positif ;
- un TIFF de tuile ne peut pas être lu, ou un fichier de sortie ne peut pas être écrit.

Au début de `execute()`, `mosaic` est remplacé par une description vide : après un échec, l'ancienne mosaïque n'est plus publiée. Si l'écriture échoue pendant la fermeture des trois fichiers, ceux déjà renommés restent sur le disque ; l'empreinte a changé, le step sera refait.

## Checkpoint

`Mosaic` est sérialisé dans `pipelineTypes()` ; les pixels restent dans les TIFF.

```json
{ "imagePath": "C:/Users/…/Temp/kikirpa-fusion/mosaic_image.tif",
  "provenancePath": "C:/Users/…/Temp/kikirpa-fusion/mosaic_provenance.tif",
  "maskPath": "C:/Users/…/Temp/kikirpa-fusion/mosaic_mask.tif",
  "canvasSize": [17400, 6810] }
```

`externalInputsFingerprint()` suit l'existence, la taille et la date des trois fichiers : si l'un disparaît ou change, le checkpoint est périmé et l'assemblage est refait à la reprise.

## Mémoire

Par bloc : le bloc lui-même (512² × 9 octets, environ 2,4 Mo), la région lue d'une tuile et sa copie corrigée (au plus un bloc chacune), le masque de territoire, et un bloc de tampon par writer. La mémoire ne dépend ni de la taille de la mosaïque ni de celle des tuiles. Le TIFF d'une tuile est rouvert à chaque lecture (`readRegion` ouvre et ferme le fichier).

## Limites

- Pas de fondu le long de la couture : chaque pixel vient d'une seule tuile.
- La rotation des positions est ignorée (comme `graphcut`, et `absolute-pos` donne toujours 0).
- Le calcul des coins est écrit deux fois (ici et dans `processImages`) : `FusionStepTest.ComputesTheSameCornersAsGraphCut` vérifie qu'ils concordent. `processImages` pourra appeler `computeMosaicCorners` quand `image_pretreatment.cpp` sera repris.
- Deux pipelines lancés en même temps avec le dossier par défaut écriraient au même endroit.

## Tests

`tests/steps/test_fusion_step.cpp` (images synthétiques 16 bits, TIFF pyramidaux dans un dossier temporaire) :

- pixels, provenance et masque des deux côtés d'une couture connue, canaux en RGB ;
- positions négatives : même mosaïque après une translation commune ;
- mosaïque de 700 × 600 (2 × 2 blocs, blocs de bord) comparée pixel par pixel ;
- contour d'un trou attribué au territoire qui l'entoure ;
- division par le gain, tuile isolée non corrigée, tuiles 8 bits passées en 16 bits ;
- échecs : position manquante (aucune mosaïque publiée, aucun fichier), nom de fichier sans `_rLL_cCC` ;
- coins identiques à ceux de `processImages` pour des positions fractionnaires et négatives ;
- bout en bout `fusion` → `export` ; empreinte qui suit les fichiers produits ; chaînage accepté après `graphcut`, refusé sans.

`tests/brightness_equalization/test_region_correction.cpp` vérifie que corriger une tuile région par région redonne `correctTile`.
