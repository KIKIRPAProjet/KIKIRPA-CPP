# MosaicCropStepClass

Fichiers :
- `include/backend/steps/mosaic_crop_step.h`
- `src/backend/steps/mosaic_crop_step.cpp`

Le step conserve un rectangle d'une mosaïque stockée en blocs PNG.
Il appelle `CroppingNs::cropMosaic`.

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ |
|---|---|---|---|
| Entrée | `mosaic` | `MosaicFiles` | `DataNs::MosaicFiles` |
| Sortie | `mosaic` | `MosaicFiles` | `DataNs::MosaicFiles` |

L'entrée contient le dossier des blocs PNG et leur nombre.
Le résultat remplace la même clé uniquement en cas de succès.
En cas d'échec, l'entrée du DataStore reste inchangée.

Le résultat contient les fichiers couleur, provenance et masque.
Le coin supérieur gauche du rectangle devient l'origine `(0, 0)`.

`getResult()` contient `error`, `totalBlocks` et, en cas de succès,
`mosaicPath`.

## Paramètres

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `x` | int | 0 | Abscisse dans le repère des blocs sources |
| `y` | int | 0 | Ordonnée dans le repère des blocs sources |
| `width` | int | 0 | Largeur strictement positive |
| `height` | int | 0 | Hauteur strictement positive |
| `outputDirectory` | string | vide | Nouveau dossier de sortie |

Les coordonnées sont en pixels pleine résolution.
Les bords droit et inférieur sont exclus.
Le parent du dossier de sortie doit exister.

Le frontend doit convertir le rectangle sélectionné sur l'aperçu en
coordonnées pleine résolution, dans le repère des blocs sources, en tenant
compte du zoom, du déplacement de l'affichage et de l'origine de la mosaïque.

Le step ne réalise pas cette conversion. Il reçoit directement les paramètres
`x`, `y`, `width` et `height`. Le coin supérieur gauche du résultat rogné
devient `(0, 0)`.

## Fonctionnement

Le step lit ses paramètres et la mosaïque, appelle l'algorithme,
puis publie le résultat.

Voir [MosaicCrop.md](../algorithms/cropping/MosaicCrop.md).

Le rognage est facultatif : omettre le step conserve la mosaïque entière.

## Erreurs

`execute()` renvoie `false` et renseigne `result["error"]` :
DataStore absent, entrée absente ou de mauvais type, paramètres invalides,
mosaïque invalide ou échec de lecture ou d'écriture.

Aucun dossier existant n'est écrasé.
Une nouvelle exécution avec le même dossier de sortie échoue si ce
dossier existe déjà. Utiliser un nouveau dossier pour un nouveau résultat.

## Checkpoint

La sortie est enregistrée dans `mosaic.json` :

    {
      "mosaicPath": "cropped_output",
      "totalBlocks": 4
    }

Le checkpoint conserve le descripteur, sans recopier les PNG.
Le dossier et ses fichiers doivent rester disponibles et inchangés.
Le chargement vérifie l'existence du dossier et un nombre de blocs positif ;
il ne contrôle pas intégralement le contenu des PNG.

## Mémoire

Le step manipule des descriptions de fichiers.
L'algorithme décode les blocs partiels un fichier à la fois.

## Tests

`tests/steps/test_mosaic_crop_step.cpp` couvre la publication du résultat,
les entrées absentes ou incorrectes, les erreurs, les paramètres JSON,
les exécutions successives et la sérialisation du descripteur.

## Intégration

L'assemblage doit publier `DataNs::MosaicFiles` sous la clé `mosaic`.
L'export doit être adapté pour consommer ce contrat.
L'export actuel utilisant `CompositeBlocks` n'est pas directement compatible.