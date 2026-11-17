# Step `mosaic-crop`

Rogne la mosaïque après `fusion` et avant `export` :

    fusion → mosaic-crop → export

## Contrat

| Sens | Type C++ | Type pipeline | Clé |
|---|---|---|---|
| Entrée | `DataNs::Mosaic` | `Mosaic` | `mosaic` |
| Sortie | `DataNs::Mosaic` | `Mosaic` | `mosaic` |

La même clé est remplacée uniquement après réussite du rognage.
Un échec conserve la mosaïque d'entrée et renseigne `getResult()["error"]`.
Les checkpoints utilisent le type `Mosaic` déjà enregistré par le pipeline.

## Paramètres

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `x` | `int` | 0 | Abscisse du coin supérieur gauche |
| `y` | `int` | 0 | Ordonnée du coin supérieur gauche |
| `width` | `int` | 0 | Largeur strictement positive |
| `height` | `int` | 0 | Hauteur strictement positive |
| `outputDirectory` | `string` | vide | Nouveau dossier obligatoire |

Le rectangle est exprimé en pixels pleine résolution dans le repère de la
mosaïque assemblée. Il doit être non vide, avec x et y positifs ou nuls,
et entièrement inclus dans `canvasSize`.

L'origine du résultat devient `(0, 0)` et sa taille devient `(width, height)`.

## Fichiers et reprise

La sortie comporte trois TIFF tuilés : `mosaic_image.tif`,
`mosaic_provenance.tif` et `mosaic_mask.tif`.

Le dossier de sortie doit être inexistant et son parent doit déjà exister.
Aucun dossier existant n'est écrasé. Pour recalculer le rognage, choisir un
nouveau dossier ou déplacer l'ancien résultat avant de reprendre.

Le compte rendu contient `image` et `canvasSize`.
L'empreinte du step suit les trois fichiers produits : une suppression ou une
modification invalide le checkpoint.

Ce step ne raccorde pas à lui seul le bouton de rognage de l'interface.

## Tests

Les tests vérifient le contrat `Mosaic`, la publication, l'empreinte des fichiers,
la conservation de l'entrée en cas d'échec et le refus des entiers hors plage.

Voir [l'algorithme](../algorithms/cropping/MosaicCrop.md) pour le traitement
par blocs et les limites.