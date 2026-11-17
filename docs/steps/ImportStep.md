# ImportStepClass

Code : `steps/import_step.h` / `.cpp` (namespace `StepsNs`). Écriture du TIFF : `core/io/pyramidal_tiff_writer` ([PyramidalTiff.md](../core/PyramidalTiff.md)).

Premier step de tout pipeline. Il crée la fiche de chaque tuile (`DataNs::TileClass`) : son **identifiant**, son **fichier** et sa **case** dans la grille (ligne, colonne). Il relie ensuite chaque tuile à ses voisines. Pour chaque image, il écrit aussi un TIFF pyramidal tuilé. Il ne modifie jamais les images d'origine.

**C'est le seul step qui sait d'où vient une tuile.** Tous les steps suivants n'utilisent que son identifiant (`TileClass::getId()`) ; aucun ne relit le nom des fichiers.

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | — | — | — | les images, lues sur le disque |
| Sortie | `tiles` | `Tiles` | `DataNs::TileList` = `std::vector<std::shared_ptr<DataNs::TileClass>>` | tous les autres steps |

- **Lit** les images données par la liste `tiles`, ou, si elle est vide, les fichiers du dossier `path` dont le nom correspond à `format`. Le step n'a aucune entrée dans le DataStore : c'est le premier.
- **Écrit** dans `tiles` une fiche par image, dans l'ordre ligne puis colonne. Une fiche ne contient aucun pixel : seulement l'identifiant, la case, les chemins de la source et du TIFF pyramidal, les dimensions, le type OpenCV et les voisines (voir « Ce que contient une fiche »).
- **Écrit aussi sur le disque**, hors du DataStore, un TIFF pyramidal par image : `<path>/.cache/pyramids/<nom>_pyramid.tif`.

`getResult()` : `tiles` (nombre de tuiles créées) ou `error`.

## Paramètres

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `path` | string | `images/` | dossier des images (non récursif) ; les fichiers relatifs de `tiles` y sont cherchés, et le cache `.cache/pyramids` y est créé |
| `tiles` | json | `[]` | **liste des tuiles** : pour chacune, son `id`, son fichier et sa case (voir ci-dessous). Vide : on utilise `format` |
| `format` | string | `img_C(\d+)_R(\d+)\.tif` | utilisé seulement si `tiles` est vide : expression régulière sur le **nom complet** du fichier, insensible à la casse, **groupe 1 = colonne, groupe 2 = ligne** |

### Deux façons de donner les tuiles

**1. La liste `tiles` (recommandée, écrite par l'interface).** Les noms de fichiers sont libres : personne n'a à renommer ses images.

```json
{ "name": "import", "parameters": [
    { "valueType": "string", "name": "path",  "value": "D:/oeuvres/VanDerVeken" },
    { "valueType": "json",   "name": "tiles", "value": [
        { "id": 0, "file": "2_no filter f8 v30 20cm.tif", "row": 0, "column": 0 },
        { "id": 1, "file": "3_no filter f8 v30 20cm.tif", "row": 0, "column": 1 },
        { "id": 2, "file": "1_no filter f8 v30 20cm.tif", "row": 1, "column": 0 },
        { "id": 3, "file": "4_no filter f8 v30 20cm.tif", "row": 1, "column": 1 } ] } ] }
```

- `file` : relatif au dossier `path`, ou absolu.
- `row`, `column` : la case, à partir de 0 (ligne 0 = en haut, colonne 0 = à gauche).
- `id` : **le rang de la tuile dans l'ordre ligne par ligne, puis colonne par colonne** (0, 1, 2…). C'est la même règle qu'avec `format`. Il est écrit dans la liste pour que le script dise clairement quel identifiant correspond à quel fichier. Un `id` qui ne respecte pas cette règle est refusé.
- L'ordre des entrées dans la liste ne compte pas.

**2. Le format des noms (`tiles` vide).** Le step prend les fichiers de `path` dont le nom correspond à `format` et y lit la colonne et la ligne. Un fichier dont le nom ne correspond pas est ignoré sans message. C'est le comportement historique : les scripts existants continuent de fonctionner.

## Fonctionnement

1. Liste les images : celles de `tiles`, ou les fichiers du dossier qui correspondent à `format`.
2. Les trie **ligne par ligne, puis colonne par colonne**. L'identifiant d'une tuile (`TileClass::getId()`) est sa place dans cet ordre : 0, 1, 2… C'est cet identifiant que les autres steps utilisent (`tileIdA`, `tileIdB`, `tileId` des positions…). Deux tuiles dans la même case font échouer le step.
3. Pour chaque image : la lit en entier (`cv::imread`, `IMREAD_UNCHANGED`, ce qui garde les 16 bits), écrit `<path>/.cache/pyramids/<nom>_pyramid.tif`, crée la fiche avec sa case (`setGridPosition`), puis libère l'image. **Une seule image est en mémoire à la fois.**
4. Relie les voisines (gauche, droite, haut, bas) d'après les cases. Une case vide de la grille donne une voisine nulle.

Le TIFF pyramidal est **réécrit à chaque exécution** du step, même s'il existe déjà. Seule la reprise par checkpoint évite de refaire l'import.

## Ce que contient une fiche

`TileClass` ne contient **aucun pixel** :

- `id` : l'identifiant de la tuile, utilisé par tous les steps ;
- `gridRow`, `gridColumn` : sa case dans la grille (`getGridRow()`, `getGridColumn()`) ;
- `source` : chemin de l'image d'origine ;
- `tiff` : chemin du TIFF pyramidal ;
- `type` : type OpenCV de l'image, par exemple `CV_16UC3` = 18 ;
- les dimensions des 4 niveaux de la pyramide (pleine résolution, 1/2, 1/4, 1/8) ;
- les pointeurs vers les voisines.

`TileList` est un vecteur de `shared_ptr`, pour que les copies du DataStore partagent les mêmes fiches et que les pointeurs vers les voisines restent valides.

## Ce qui est écrit dans le checkpoint (`tiles.json`)

```json
[
  { "id": 0, "source": "D:/oeuvre/tuiles/2_no filter.tif", "row": 0, "column": 0,
    "tiff": "D:/oeuvre/tuiles/.cache/pyramids/2_no filter_pyramid.tif",
    "width": 9000, "height": 7000, "type": 18, "left": -1, "right": 1, "top": -1, "bottom": 2 },
  { "id": 1, "source": "D:/oeuvre/tuiles/3_no filter.tif", "row": 0, "column": 1,
    "tiff": "D:/oeuvre/tuiles/.cache/pyramids/3_no filter_pyramid.tif",
    "width": 9000, "height": 7000, "type": 18, "left": 0, "right": -1, "top": -1, "bottom": 3 }
]
```

Les valeurs de cet exemple sont inventées ; la forme est celle de `saveTiles` (`core/checkpoint/checkpoint_types.cpp`). `-1` = pas de voisine. À la reprise, le TIFF pyramidal doit toujours exister.

## Empreinte et reprise

`externalInputsFingerprint()` liste, pour chaque image retenue, son nom (son chemin complet avec la liste `tiles`), sa taille et sa date. Ajouter, retirer ou modifier une image périme le checkpoint de l'import, et tous les steps suivants sont refaits. Modifier la liste `tiles` change les paramètres du step, ce qui périme aussi le checkpoint.

Un checkpoint écrit avant l'ajout du paramètre `tiles` est jugé périmé (ses paramètres diffèrent) : l'import et la suite sont refaits.

## Erreurs

`execute()` renvoie `false`, avec la cause dans `error`, si :

- `path` n'est pas un dossier ;
- `tiles` n'est pas un tableau, ou une entrée n'a pas la forme `{ id, file, row, column }` (fichier en texte, le reste en entiers, ligne et colonne positives) ;
- un fichier de la liste est absent ou listé deux fois ;
- un `id` de la liste n'est pas le rang de sa tuile dans l'ordre ligne par ligne, puis colonne par colonne ;
- (sans liste) `format` n'est pas une expression valide ou a moins de 2 groupes, ou aucun fichier ne correspond ;
- deux tuiles occupent la même case ;
- une image est illisible ;
- un TIFF ne peut pas être écrit.

## Tests

`tests/steps/test_import_step.cpp` : liste avec des noms libres (identifiant, fichier, case, voisines), ordre de la liste sans effet, case lue dans le nom avec `format`, `id` mal placé, deux tuiles dans la même case, fichier absent, entrée mal formée, empreinte qui suit les fichiers de la liste. Le step est aussi exécuté en début de pipeline par les tests des steps qui le suivent (`tests/steps/test_sift_step.cpp`, `test_lightglue_step.cpp`, `tests/intensity_correction/test_intensity_correction.cpp`), et son enregistrement est vérifié par `tests/core/test_pipeline.cpp`.
