# ExportStepClass

Code : [export_step.h](../../include/backend/steps/export_step.h) / [export_step.cpp](../../src/backend/steps/export_step.cpp), namespace `StepsNs`.

Dernier step du pipeline. Il **publie** l'image de la mosaïque, déjà écrite sur disque par l'assemblage, sous le nom `mosaic.tif` (ou un nom configuré) dans le dossier choisi. Il ne crée ni fichier JSON, ni fichier de provenance, ni masque séparé, et les pixels ne passent pas par la mémoire : le fichier est copié tel quel. Si un fichier du même nom existe déjà dans le dossier de destination, il n'est pas écrasé : le nouvel export prend le numéro qui suit le plus élevé présent (` (1)`, ` (2)`...).

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | `mosaic` | `Mosaic` | `DataNs::Mosaic` | `fusion` |
| Sortie | — | — | — | aucune : ce step termine le pipeline ; il écrit `mosaic.tif` (ou nom incrémenté) |

`DataNs::Mosaic` est défini dans `core/data/algorithm_types.h`. C'est une description légère : les chemins de trois TIFF tuilés (image, provenance, masque) et `canvasSize`, les dimensions de la mosaïque. L'export lit seulement :

- `imagePath` : TIFF tuilé RGB 16 bits, écrit bloc par bloc par l'assemblage avec `IONs::TiledTiffWriterClass` ([core/TiledTiffWriter.md](../core/TiledTiffWriter.md)) ;
- `canvasSize` : comparé aux dimensions lues dans l'en-tête du TIFF.

`provenancePath` et `maskPath` ne sont pas exportés.

`getOutput()` fournit le chemin publié, les dimensions et l'indication TIFF/BigTIFF (lue dans l'en-tête). `getResult()` contient `image` (`path`, `bigTiff`), `canvasSize` et, si `removeMosaicCache` a été demandé, `removedMosaicCacheFiles`, ou `error`.

## Paramètres

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `outputDirectory` | string | vide | dossier où publier l'image ; vide = dossier Téléchargements de l'utilisateur courant. Créé s'il n'existe pas |
| `filename` | string | `mosaic.tif` | nom du fichier de sortie. S'il n'a pas d'extension, `.tif` est ajouté. S'il existe déjà dans le dossier, un suffixe ` (1)`, ` (2)`... est ajouté |
| `removeMosaicCache` | bool | `false` | supprimer le cache de la mosaïque après une copie réussie (voir ci-dessous) |

## Fonctionnement

1. **Contrôle** : l'en-tête de `imagePath` est lu sans décoder de pixel. Le fichier doit être un TIFF tuilé, à 3 canaux de 16 bits, aux dimensions de `canvasSize`.
2. **Nom unique** : si `<filename>` ou une de ses copies numérotées existe déjà dans le dossier de sortie, le step prend le numéro qui suit le plus élevé présent : `<nom> (1).tif`, `<nom> (2).tif`, etc. Le dernier export est ainsi toujours celui de numéro maximal, même si des copies intermédiaires ont été supprimées.
3. **Publication** : `IONs::copyFileReplacing` copie le fichier sous un nom temporaire dans `outputDirectory`, puis le renomme vers le chemin unique déterminé. L'ancien fichier existant n'est donc jamais écrasé. Une erreur pendant la copie ne pollue pas la destination et le temporaire est supprimé.

Le format du fichier (blocs de 512 × 512, Deflate avec prédicteur horizontal, TIFF ou BigTIFF selon la taille) est celui qu'a choisi le writer de l'assemblage : l'export ne le réécrit pas. C'est ce qui permet de supprimer la mosaïque après coup : le fichier publié en est une copie au bit près.

**Copie et non déplacement** : la sortie de l'assemblage reste en place. On peut ainsi refaire l'export (par exemple vers un autre dossier) sans refaire l'assemblage, dont le checkpoint reste valable. Le prix est la place sur le disque : l'image existe deux fois. Si `imagePath` est déjà le fichier de destination, rien n'est copié.

Les fichiers annexes d'anciennes exécutions ne sont pas supprimés.

## Supprimer le cache de la mosaïque

Avec `removeMosaicCache` à `true`, les **trois** TIFF écrits par `fusion` dans `<dossier des images>/.cache/mosaic`
(image, provenance, masque) sont supprimés après une copie réussie, et leur dossier avec eux s'il ne reste rien
dedans. Sur une grille 10 × 10, c'est **40 à 60 Gio** récupérés pour une perte quasi nulle : l'image publiée est une
copie exacte de la mosaïque, et `fusion` sait la refaire.

Deux garde-fous :

- **après** la copie, jamais avant : la mosaïque n'est jetable qu'une fois publiée ailleurs ;
- **jamais le fichier qui vient d'être publié**. Si le dossier de sortie tombe sur le cache, la destination peut
  être l'un des trois fichiers, et `copyFileReplacing` n'a alors rien copié : le supprimer détruirait l'export.
  La numérotation du nom rend ce cas pratiquement inatteignable aujourd'hui, mais une option `overwrite` (prévue
  par #133) le rendrait réel.

Et deux conséquences, qui expliquent le défaut à `false` :

- une **reprise** (`--resume`) refera `fusion`, dont l'empreinte porte justement sur ces trois fichiers, puis
  l'export, qui écrira une copie de plus (` (1)`). C'est cohérent, mais ce n'est pas gratuit sur une grande
  mosaïque ;
- l'aperçu de la mosaïque de l'interface n'a plus rien à lire.

Une suppression qui échoue ne fait pas échouer l'export : le compte réel est dans `removedMosaicCacheFiles`.

## Erreurs

`execute()` renvoie `false`, avec la cause dans `result["error"]`, si :

- l'entrée `mosaic` manque ;
- `canvasSize` n'est pas strictement positive ;
- l'image est introuvable ou illisible ;
- ses dimensions diffèrent de `canvasSize`, ou ce n'est pas un TIFF tuilé RGB 16 bits ;
- le dossier de sortie ne peut pas être créé, ou la copie ou le renommage échoue.

## Checkpoint

L'export n'a pas de sortie dans le DataStore. Son empreinte de reprise (`externalInputsFingerprint`) suit l'existence, la taille et la date de modification du dernier fichier publié. Après `execute()`, c'est le fichier qui vient d'être écrit ; à la reprise d'un run, avant `execute()`, le step le retrouve sur le disque (numéro maximal). Tant que ce fichier est là, inchangé, le checkpoint reste valable et la reprise ne crée pas de copie de plus ; s'il est supprimé ou modifié, l'export est refait.

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

`tests/core/test_export_step.cpp` (cible `export_step_tests`) : publication par défaut sous `mosaic.tif`, incrémentation automatique sans écrasement (`mosaic (1).tif`...), support d'un nom de fichier personnalisé, contenu relu identique, sortie de l'assemblage conservée ; refus d'une image aux mauvaises dimensions ou qui n'est pas RGB 16 bits ; image ou entrée manquante ; empreinte qui suit le fichier publié, y compris pour un step neuf à la reprise ; numéro qui suit le plus élevé présent ; suppression du cache de la mosaïque demandée (les fichiers et leur dossier partent, l'export reste, le compte est dans `removedMosaicCacheFiles`), et les deux cas où elle ne l'est pas (défaut, et paramètre à faux : rien n'est supprimé et la clé est absente du résultat). Les images sont synthétiques, écrites avec `TiledTiffWriterClass` dans un dossier temporaire.

```text
cmake --build --preset windows-aqt --target export_step_tests
ctest --preset windows-aqt -R ExportStepTest
```

Remplacer `windows-aqt` par `linux` ou `macos` selon le système.
