# ExportStepClass

Code : [export_step.h](../../include/backend/steps/export_step.h) / [export_step.cpp](../../src/backend/steps/export_step.cpp), namespace `StepsNs`.

Dernier step du pipeline. Il **publie** l'image de la mosaïque, déjà écrite sur disque par l'assemblage, sous le nom `mosaic.tif` dans le dossier choisi. Il ne crée ni fichier JSON, ni fichier de provenance, ni masque séparé, et les pixels ne passent pas par la mémoire : le fichier est copié tel quel.

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | `mosaic` | `Mosaic` | `DataNs::Mosaic` | `fusion` |
| Sortie | — | — | — | aucune : ce step termine le pipeline ; il écrit `mosaic.tif` |

`DataNs::Mosaic` est défini dans `core/data/algorithm_types.h`. C'est une description légère : les chemins de trois TIFF tuilés (image, provenance, masque) et `canvasSize`, les dimensions de la mosaïque. L'export lit seulement :

- `imagePath` : TIFF tuilé RGB 16 bits, écrit bloc par bloc par l'assemblage avec `IONs::TiledTiffWriterClass` ([core/TiledTiffWriter.md](../core/TiledTiffWriter.md)) ;
- `canvasSize` : comparé aux dimensions lues dans l'en-tête du TIFF.

`provenancePath` et `maskPath` ne sont pas exportés.

`getOutput()` fournit le chemin publié, les dimensions et l'indication TIFF/BigTIFF (lue dans l'en-tête). `getResult()` contient `image` (`path`, `bigTiff`) et `canvasSize`, ou `error`.

## Paramètre

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `outputDirectory` | string | vide | dossier où publier `mosaic.tif` ; vide = dossier Téléchargements de l'utilisateur courant. Créé s'il n'existe pas |

## Fonctionnement

1. **Contrôle** : l'en-tête de `imagePath` est lu sans décoder de pixel. Le fichier doit être un TIFF tuilé, à 3 canaux de 16 bits, aux dimensions de `canvasSize`.
2. **Publication** : `IONs::copyFileReplacing` copie le fichier sous un nom temporaire dans `outputDirectory`, puis le renomme en `mosaic.tif`, en remplaçant un éventuel ancien fichier. Une erreur pendant la copie ne détruit donc pas la mosaïque existante, et le temporaire est supprimé.

Le format du fichier (blocs de 512 × 512, LZW, TIFF ou BigTIFF selon la taille) est celui qu'a choisi le writer de l'assemblage : l'export ne le réécrit pas.

**Copie et non déplacement** : la sortie de l'assemblage reste en place. On peut ainsi refaire l'export (par exemple vers un autre dossier) sans refaire l'assemblage, dont le checkpoint reste valable. Le prix est la place sur le disque : l'image existe deux fois. Si `imagePath` est déjà `outputDirectory/mosaic.tif`, rien n'est copié.

Les fichiers annexes d'anciennes exécutions ne sont pas supprimés.

## Erreurs

`execute()` renvoie `false`, avec la cause dans `result["error"]`, si :

- l'entrée `mosaic` manque ;
- `canvasSize` n'est pas strictement positive ;
- l'image est introuvable ou illisible ;
- ses dimensions diffèrent de `canvasSize`, ou ce n'est pas un TIFF tuilé RGB 16 bits ;
- le dossier de sortie ne peut pas être créé, ou la copie ou le renommage échoue.

## Checkpoint

L'export n'a pas de sortie dans le DataStore. Son empreinte de reprise (`externalInputsFingerprint`) suit l'existence, la taille et la date de modification de `mosaic.tif` : si le fichier publié est supprimé ou modifié, le checkpoint de ce step est périmé et l'export est refait.

L'entrée `Mosaic` a sa sérialisation dans `pipelineTypes()` (`core/checkpoint/checkpoint_types.cpp`) : seuls les chemins et les dimensions sont écrits, les pixels restent dans les TIFF.

```json
{ "imagePath": "D:/oeuvre/tuiles/.cache/fusion/mosaic_image.tif",
  "provenancePath": "D:/oeuvre/tuiles/.cache/fusion/mosaic_provenance.tif",
  "maskPath": "D:/oeuvre/tuiles/.cache/fusion/mosaic_mask.tif",
  "canvasSize": [17400, 6810] }
```

## Mémoire

Aucune image n'est chargée : l'en-tête est lu par libtiff et la copie passe par le système de fichiers.

## État de l'enchaînement

`export` se place après `fusion`, qui produit `mosaic` ([FusionStep.md](FusionStep.md)) ; sans lui, `buildPipeline` refuse le script. Le test de bout en bout `fusion` → `export` est dans `tests/steps/test_fusion_step.cpp`.

## Tests

`tests/core/test_export_step.cpp` (cible `export_step_tests`) : publication de l'image seule à la place d'un ancien `mosaic.tif`, contenu relu identique, sortie de l'assemblage conservée ; refus d'une image aux mauvaises dimensions ou qui n'est pas RGB 16 bits ; image ou entrée manquante ; empreinte qui suit le fichier publié. Les images sont synthétiques, écrites avec `TiledTiffWriterClass` dans un dossier temporaire.

```text
cmake --build --preset windows-aqt --target export_step_tests
ctest --preset windows-aqt -R ExportStepTest
```

Remplacer `windows-aqt` par `linux` ou `macos` selon le système.
