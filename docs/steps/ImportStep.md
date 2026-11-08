# ImportStepClass

Code : `steps/import_step.h` / `.cpp` (namespace `StepsNs`). Écriture du TIFF : `core/io/pyramidal_tiff_writer` ([PyramidalTiff.md](../core/PyramidalTiff.md)).

Premier step de tout pipeline. Il repère les images d'un dossier, lit la ligne et la colonne de chacune dans son nom, crée sa fiche (`DataNs::TileClass`) et relie chaque tuile à ses voisines. Pour chaque image, il écrit aussi un TIFF pyramidal tuilé. Il ne modifie jamais les images d'origine.

## Contrat

| Sens | Clé | Type JSON | Type C++ |
|---|---|---|---|
| Entrée | — | — | (lit le disque) |
| Sortie | `tiles` | `Tiles` | `DataNs::TileList` = `std::vector<std::shared_ptr<DataNs::TileClass>>` |

`getResult()` : `tiles` (nombre de tuiles créées) ou `error`.

## Paramètres

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `path` | string | `images/` | dossier des images (non récursif) |
| `format` | string | `img_C(\d+)_R(\d+)\.tif` | expression régulière sur le **nom complet** du fichier, insensible à la casse : **groupe 1 = colonne, groupe 2 = ligne** |

Un fichier dont le nom ne correspond pas au format est ignoré sans message.

## ⚠️ Le format par défaut ne convient pas à la suite du pipeline

`absolute-pos` et `graphcut` relisent la ligne et la colonne dans le nom du fichier, avec un motif fixe : le nom doit **finir par `_r<ligne>_c<colonne>.tif`** (ou `.tiff`) (`GraphCutNs::ImagePretreatmentClass::parseTileName`). Avec le format par défaut (`img_C…_R…`), ces deux steps échouent.

Pour des fichiers `…_r00_c01.tif`, donner à `import` un format qui met la colonne dans le groupe 1 et la ligne dans le groupe 2. Une anticipation `(?=…)` capture d'abord la colonne, placée après la ligne dans le nom :

```
.*_r(?=\d+_c(\d+)\.tiff?$)(\d+)_c\d+\.tiff?
```

Dans le JSON, chaque `\` est doublé :

```json
{ "name": "import",
  "parameters": [
    { "valueType": "string", "name": "path", "value": "D:/oeuvre/tuiles" },
    { "valueType": "string", "name": "format", "value": ".*_r(?=\\d+_c(\\d+)\\.tiff?$)(\\d+)_c\\d+\\.tiff?" }
  ] }
```

Unifier les deux conventions de nommage est une tâche à faire.

## Fonctionnement

1. Liste les fichiers du dossier qui correspondent à `format`.
2. Les trie **ligne par ligne, puis colonne par colonne**. L'identifiant d'une tuile (`TileClass::getId()`) est sa place dans cet ordre : 0, 1, 2… C'est cet identifiant que les autres steps utilisent (`tileIdA`, `tileIdB`…).
3. Pour chaque image : la lit en entier (`cv::imread`, `IMREAD_UNCHANGED`, ce qui garde les 16 bits), écrit `<path>/.cache/pyramids/<nom>_pyramid.tif`, crée la fiche, puis libère l'image. **Une seule image est en mémoire à la fois.**
4. Relie les voisines (gauche, droite, haut, bas) d'après les colonnes et les lignes. Une case vide de la grille donne une voisine nulle.

Le TIFF pyramidal est **réécrit à chaque exécution** du step, même s'il existe déjà. Seule la reprise par checkpoint évite de refaire l'import. Aujourd'hui, aucun step ne relit ce TIFF : tous relisent l'image source (`getSource()`).

## Ce que contient une fiche

`TileClass` ne contient **aucun pixel** :

- `id` ;
- `source` : chemin de l'image d'origine ;
- `tiff` : chemin du TIFF pyramidal ;
- `type` : type OpenCV de l'image, par exemple `CV_16UC3` = 18 ;
- les dimensions des 4 niveaux de la pyramide (pleine résolution, 1/2, 1/4, 1/8) ;
- les pointeurs vers les voisines.

`TileList` est un vecteur de `shared_ptr`, pour que les copies du DataStore partagent les mêmes fiches et que les pointeurs vers les voisines restent valides.

## Ce qui est écrit dans le checkpoint (`tiles.json`)

```json
[
  { "id": 0, "source": "D:/oeuvre/tuiles/t_r00_c00.tif", "tiff": "D:/oeuvre/tuiles/.cache/pyramids/t_r00_c00_pyramid.tif",
    "width": 9000, "height": 7000, "type": 18, "left": -1, "right": 1, "top": -1, "bottom": 2 },
  { "id": 1, "source": "D:/oeuvre/tuiles/t_r00_c01.tif", "tiff": "D:/oeuvre/tuiles/.cache/pyramids/t_r00_c01_pyramid.tif",
    "width": 9000, "height": 7000, "type": 18, "left": 0, "right": -1, "top": -1, "bottom": 3 }
]
```

Les valeurs de cet exemple sont inventées ; la forme est celle de `saveTiles` (`core/checkpoint/checkpoint_types.cpp`). `-1` = pas de voisine. À la reprise, le TIFF pyramidal doit toujours exister.

## Empreinte et reprise

`externalInputsFingerprint()` liste le nom, la taille et la date de chaque image retenue. Ajouter, retirer ou modifier une image périme le checkpoint de l'import, et tous les steps suivants sont refaits.

## Erreurs

`execute()` renvoie `false`, avec la cause dans `error`, si :

- `path` n'est pas un dossier ;
- `format` n'est pas une expression valide ou a moins de 2 groupes ;
- aucun fichier ne correspond ;
- une image est illisible ;
- un TIFF ne peut pas être écrit.

## Tests

Il n'y a pas de test dédié à ce step. Il est exécuté en début de pipeline par les tests des steps qui le suivent (`tests/steps/test_sift_step.cpp`, `test_lightglue_step.cpp`, `test_absolute_pos_step.cpp`, `tests/intensity_correction/test_intensity_correction.cpp`), et son enregistrement est vérifié par `tests/core/test_pipeline.cpp`. L'empreinte n'est pas testée.
