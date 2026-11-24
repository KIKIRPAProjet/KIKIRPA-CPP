# MosaicCropStepClass

Code : [mosaic_crop_step.h](../../include/backend/steps/mosaic_crop_step.h) /
[mosaic_crop_step.cpp](../../src/backend/steps/mosaic_crop_step.cpp),
namespace `StepsNs`.

Rogne un rectangle dans la mosaïque assemblée. Ce step est facultatif et
se place après `fusion`, avant `export` :

    fusion → mosaic-crop → export

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | `mosaic` | `Mosaic` | `DataNs::Mosaic` | `fusion` |
| Sortie | `mosaic` | `Mosaic` | `DataNs::Mosaic` | `export` |

- **Lit** les chemins des trois TIFF sources et `canvasSize`.
- **Écrit** les chemins des trois TIFF rognés et leur nouvelle taille.
- Remplace la clé `mosaic` uniquement après réussite.
- Conserve la mosaïque d'entrée en cas d'échec et renseigne
  `getResult()["error"]`.

Les checkpoints utilisent le type `Mosaic` déjà enregistré par le pipeline.

## Paramètres

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `x` | `int` | 0 | Abscisse du coin supérieur gauche |
| `y` | `int` | 0 | Ordonnée du coin supérieur gauche |
| `width` | `int` | 0 | Largeur strictement positive |
| `height` | `int` | 0 | Hauteur strictement positive |
| `outputDirectory` | `string` | vide | Dossier de sortie obligatoire, dont le parent doit exister |

Le rectangle est exprimé en pixels pleine résolution dans le repère de la
mosaïque assemblée. Il doit être non vide, avec `x` et `y` positifs ou nuls,
et entièrement inclus dans `canvasSize`.

L'origine du résultat devient `(0, 0)` et sa taille devient `(width, height)`.

## Fichiers produits

| Fichier | Type | Contenu |
|---|---|---|
| `mosaic_image.tif` | `CV_16UC3` | Image RGB 16 bits |
| `mosaic_provenance.tif` | `CV_16UC1` | Identifiants des tuiles sources |
| `mosaic_mask.tif` | `CV_8UC1` | Masque de validité |

Les trois fichiers sont découpés avec le même rectangle, bloc par bloc.
Le compte rendu contient `image` et `canvasSize`.

## Publication et relance

Les trois TIFF sont écrits et fermés dans un dossier temporaire voisin
avant publication.

Si le dossier de destination existe déjà, il doit être identifié comme un
résultat de `mosaic-crop` par le fichier `.mosaic-crop-owner`. Il peut manquer
un ou plusieurs TIFF : une relance les recrée.

Un dossier sans marqueur valide, contenant des entrées étrangères ou des
liens symboliques est refusé. Le dossier de sortie ne doit pas contenir
les fichiers sources.

Les résultats créés par l'ancienne version, sans marqueur, doivent être
déplacés manuellement avant de réutiliser leur chemin de sortie.

Lors du remplacement :

1. L'ancien résultat est renommé en sauvegarde.
2. Le nouveau résultat prend sa place.
3. La sauvegarde est supprimée si le système le permet.

Si la publication échoue, l'ancien résultat est restauré. Si la restauration
échoue également, la sauvegarde est conservée et son chemin figure dans
le message d'erreur.

Une relance avec un autre rectangle doit repartir de la mosaïque assemblée
par `fusion`, et non du résultat déjà rogné.

## Checkpoint

L'empreinte du step suit les trois TIFF produits : une suppression ou une
modification invalide le checkpoint.

Après restauration de l'entrée issue de `fusion`, le step peut recalculer
dans le même dossier de sortie.

## Limites

- Les trois TIFF rognés occupent de l'espace supplémentaire sur disque.
  Pendant une relance, l'ancien et le nouveau résultat coexistent.
- Les renommages successifs ne constituent pas une transaction résistante
  à une interruption brutale du processus. Une restauration manuelle de
  la sauvegarde peut alors être nécessaire.
- Les exécutions simultanées vers le même dossier ne sont pas prises en charge.

- Le rectangle sélectionné dans l'interface n'est actuellement pas enregistré
  dans le fichier projet. Il doit être redéfini après réouverture.

  ## Utilisation depuis l'interface

Dans l'onglet Image finale, le bouton Rogner permet de sélectionner un
rectangle. Valider le recadrage modifie uniquement l'aperçu en mémoire,
sans lire ni écrire les TIFF pleine résolution.

Le bouton Exporter convertit le rectangle normalisé en coordonnées pleine
résolution à partir de `mosaic.canvasSize`, puis lance
`mosaic-crop → export` depuis le checkpoint de fusion.
Si ce checkpoint n'est plus valide, le calcul est refusé.

Après réussite, la fenêtre d'enregistrement s'ouvre. Un nouvel export du
même recadrage réutilise le TIFF calculé tant qu'il est disponible.

Réinitialiser restaure la mosaïque originale. Une modification de la grille
invalide également les résultats et le recadrage.

Chaque nouveau calcul de rognage utilise un dossier distinct. Les résultats
des essais précédents ne sont pas automatiquement supprimés.

## Tests

`tests/steps/test_mosaic_crop_step.cpp` vérifie :

- le contrat `Mosaic` et la publication dans le DataStore ;
- le suivi des fichiers par l'empreinte ;
- la relance dans le même dossier après suppression d'un TIFF ;
- la conservation de l'entrée en cas d'échec ;
- le refus des entiers hors plage.

Les tests de l'algorithme vérifient notamment les pixels des trois TIFF,
le changement de rectangle, la recréation des fichiers manquants et
la protection des dossiers étrangers et des sources.

Voir [MosaicCrop.md](../algorithms/cropping/MosaicCrop.md) pour les détails
de l'algorithme.