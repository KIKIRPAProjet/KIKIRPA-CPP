# ExportStepClass

Code : [export_step.h](../../include/backend/steps/export_step.h) / [export_step.cpp](../../src/backend/steps/export_step.cpp), namespace `StepsNs`.

Dernier step du pipeline. Il **publie** l'image de la mosaïque, déjà écrite sur disque par l'assemblage, sous un nom configuré dans le dossier choisi. Le format peut être TIFF 16 bits ou JPEG 8 bits. Il ne crée ni fichier JSON, ni fichier de provenance, ni masque séparé. Si un fichier du même nom existe déjà, il n'est pas écrasé : le nouvel export prend le numéro qui suit le plus élevé présent (` (1)`, ` (2)`...).

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | `mosaic` | `Mosaic` | `DataNs::Mosaic` | `fusion` |
| Sortie | — | — | — | aucune : ce step termine le pipeline ; il écrit un fichier TIFF ou JPEG |

`DataNs::Mosaic` est défini dans `core/data/algorithm_types.h`. C'est une description légère : les chemins de trois TIFF tuilés (image, provenance, masque) et `canvasSize`, les dimensions de la mosaïque. L'export lit seulement :

- `imagePath` : TIFF tuilé RGB 16 bits, écrit bloc par bloc par l'assemblage avec `IONs::TiledTiffWriterClass` ([core/TiledTiffWriter.md](../core/TiledTiffWriter.md)) ;
- `canvasSize` : comparé aux dimensions lues dans l'en-tête du TIFF.

`provenancePath` et `maskPath` ne sont pas exportés.

`getOutput()` fournit le chemin publié, les dimensions et l'indication TIFF/BigTIFF (lue dans l'en-tête). `getResult()` contient `image` (`path`, `bigTiff`, `format`) et `canvasSize`, ou `error`.

## Paramètres

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `outputDirectory` | string | vide | dossier où publier l'image ; vide = dossier Téléchargements de l'utilisateur courant. Créé s'il n'existe pas |
| `filename` | string | selon le format | nom du fichier de sortie (`mosaic.tif` pour TIFF ou `mosaic.jpg` pour JPEG). L'extension correcte est imposée selon le format |
| `format` | string | `tiff` | `tiff` conserve la mosaïque RGB 16 bits ; `jpeg` publie une image RGB 8 bits avec perte |

## Fonctionnement

1. **Contrôle** : l'en-tête de `imagePath` est lu sans décoder de pixel. Le fichier doit être un TIFF tuilé, à 3 canaux de 16 bits, aux dimensions de `canvasSize`.
2. **Nom unique** : si le nom choisi ou une de ses copies numérotées existe déjà dans le dossier de sortie, le step prend le numéro qui suit le plus élevé présent. Le dernier export est ainsi toujours celui de rang maximal.
3. **Publication** : en TIFF, `IONs::copyFileReplacing` copie le fichier sous un nom temporaire puis le renomme vers le chemin unique. En JPEG, la mosaïque est lue, convertie en 8 bits, encodée puis remplacée atomiquement.

Le format TIFF (blocs de 512 × 512, LZW, TIFF ou BigTIFF selon la taille) est celui qu'a choisi le writer de l'assemblage : l'export ne le réécrit pas.

**Copie et non déplacement** : la sortie de l'assemblage reste en place. On peut ainsi refaire l'export (par exemple vers un autre dossier) sans refaire l'assemblage. Le prix est la place sur le disque. Si `imagePath` est déjà le fichier de destination, rien n'est copié.

Les fichiers annexes d'anciennes exécutions ne sont pas supprimés.

## Erreurs

`execute()` renvoie `false`, avec la cause dans `result["error"]`, si :

- l'entrée `mosaic` manque ;
- `canvasSize` n'est pas strictement positive ;
- l'image est introuvable ou illisible ;
- ses dimensions diffèrent de `canvasSize`, ou ce n'est pas un TIFF tuilé RGB 16 bits ;
- le format demandé est inconnu ;
- le dossier de sortie ne peut pas être créé, ou la copie ou le renommage échoue.

## Checkpoint

L'export n'a pas de sortie dans le DataStore. Son empreinte de reprise suit l'existence, la taille et la date de modification du dernier fichier publié. À la reprise d'un run, avant `execute()`, le step retrouve le fichier de rang maximal. Tant que ce fichier est là, inchangé, le checkpoint reste valable ; s'il est supprimé ou modifié, l'export est refait.

L'entrée `Mosaic` a sa sérialisation dans `pipelineTypes()` (`core/checkpoint/checkpoint_types.cpp`) : seuls les chemins et les dimensions sont écrits, les pixels restent dans les TIFF.

```json
{ "imagePath": "D:/oeuvre/tuiles/.cache/fusion/mosaic_image.tif",
  "provenancePath": "D:/oeuvre/tuiles/.cache/fusion/mosaic_provenance.tif",
  "maskPath": "D:/oeuvre/tuiles/.cache/fusion/mosaic_mask.tif",
  "canvasSize": [17400, 6810] }
```

## Mémoire

En mode TIFF, aucune image n'est chargée : l'en-tête est lu par libtiff et la copie passe par le système de fichiers. En mode JPEG, une image pleine résolution est nécessaire pour l'encodage JPEG.

## État de l'enchaînement

`export` se place après `fusion`, qui produit `mosaic` ([FusionStep.md](FusionStep.md)) ; sans lui, `buildPipeline` refuse le script. Le test de bout en bout `fusion` → `export` est dans `tests/steps/test_fusion_step.cpp`.

## Tests

`tests/core/test_export_step.cpp` (cible `export_step_tests`) couvre les exports TIFF et JPEG, l'incrémentation sans écrasement, les noms personnalisés, la conservation de la sortie de l'assemblage, les erreurs de dimensions ou de format, les entrées manquantes et les empreintes de reprise.

```text
cmake --build --preset windows-aqt --target export_step_tests
ctest --preset windows-aqt -R ExportStepTest
```

Remplacer `windows-aqt` par `linux` ou `macos` selon le système.
