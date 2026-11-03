# TIFF pyramidal : écriture et lecture

Code :

- `core/io/pyramidal_tiff_writer.h` / `.cpp` : création d'un TIFF pyramidal tuilé ;
- `core/io/pyramidal_tiff_reader.h` / `.cpp` : lecture d'une région d'un niveau de la pyramide.

Tests : `tests/core/test_pyramidal_tiff_reader.cpp`.

## Principe

Un TIFF pyramidal contient plusieurs versions de la même image :

| Niveau | Résolution                              |
| ------ | --------------------------------------- |
| `0`    | résolution originale                    |
| `1`    | moitié de la largeur et de la hauteur   |
| `2`    | quart de la largeur et de la hauteur    |
| `3`    | huitième de la largeur et de la hauteur |

Le nombre de niveaux est défini par `DataNs::LEVEL_COUNT`. Chaque niveau est stocké en tuiles
de `DataNs::BLOCK_SIZE` pixels de côté. Le writer utilise actuellement une taille de 512 × 512
pixels et une compression LZW.

Les niveaux réduits sont créés successivement avec `cv::INTER_AREA`. Les dimensions sont donc
arrondies vers le bas, comme dans `TileClass`.

## Formats acceptés

Le writer accepte une matrice OpenCV 2D non vide :

- profondeur `CV_8U` ou `CV_16U` ;
- 1, 3 ou 4 canaux ;
- image en mémoire continue ou non continue.

Le format est conservé dans le TIFF. Une image 16 bits reste donc 16 bits à la lecture.
Les niveaux réduits gardent également le nombre de canaux de l'image d'origine.

## Créer un TIFF pyramidal

La fonction `IONs::createPyramidalTiledTiff` reçoit l'image source, le chemin de sortie et
une chaîne qui reçoit le détail de l'erreur :

```cpp
#include "core/io/pyramidal_tiff_writer.h"

#include <opencv2/imgcodecs.hpp>

#include <string>

cv::Mat image = cv::imread("images/input.tif", cv::IMREAD_UNCHANGED);
std::string errorMessage;

const bool success = IONs::createPyramidalTiledTiff(
    image,
    "output/input_pyramid.tif",
    errorMessage
);
if (!success) {
    // errorMessage explique pourquoi l'écriture a échoué.
}
```

Le dossier parent est créé s'il n'existe pas. En cas d'échec, le fichier TIFF incomplet est
fermé puis supprimé. La fonction renvoie `false` si l'image est vide, si son format n'est pas
accepté ou si une opération sur le fichier échoue.

## Lire une région

`IONs::PyramidalTiffReader` est construit avec le chemin d'un TIFF pyramidal. `readRegion` reçoit :

| Paramètre      | Rôle                                                   |
| -------------- | ------------------------------------------------------ |
| `level`        | niveau à lire, avec `0` pour la résolution originale   |
| `region`       | rectangle `cv::Rect` dans les coordonnées de ce niveau |
| `output`       | matrice remplie par la fonction                        |
| `errorMessage` | détail de l'erreur si la lecture échoue                |

Exemple :

```cpp
#include "core/io/pyramidal_tiff_reader.h"

#include <opencv2/core.hpp>

#include <string>

IONs::PyramidalTiffReader reader("output/input_pyramid.tif");
cv::Mat region;
std::string errorMessage;

const bool success = reader.readRegion(
    1,
    cv::Rect(100, 50, 400, 300),
    region,
    errorMessage
);
if (!success) {
    // errorMessage explique pourquoi la région n'a pas pu être lue.
}
```

Les coordonnées de `region` sont toujours exprimées dans le niveau demandé. Dans l'exemple,
`cv::Rect(100, 50, 400, 300)` désigne une région de 400 × 300 pixels du niveau `1`, et non
de la résolution originale.

La région doit avoir des coordonnées positives et une largeur et une hauteur strictement
positives. Elle doit être entièrement contenue dans le niveau demandé : le reader ne la
tronque pas automatiquement. En cas de succès, `output` a exactement la taille de `region`
et le type du niveau lu.

## Lecture par tuiles

Le reader n'a pas besoin de charger tout le niveau en mémoire. Il :

1. ouvre le TIFF ;
2. sélectionne le répertoire du niveau demandé ;
3. vérifie les dimensions et le format ;
4. identifie les tuiles qui croisent `region` ;
5. lit uniquement ces tuiles ;
6. copie leur partie utile dans `output` ;
7. ferme le fichier.

Chaque appel à `readRegion` ouvre et ferme le fichier. Les appels sont donc indépendants et
le reader ne conserve pas de descripteur TIFF ouvert entre deux lectures.

## Erreurs courantes

`readRegion` renvoie `false` dans les cas suivants :

- le niveau demandé est négatif ou n'existe pas ;
- le fichier ne peut pas être ouvert ;
- la région a une taille invalide ou sort du niveau ;
- le niveau n'est pas un TIFF tuilé compatible ;
- les métadonnées ou une tuile ne peuvent pas être lues ;
- le format de l'image n'est pas supporté.

Dans tous ces cas, `errorMessage` contient une description en anglais, conformément aux
conventions des messages d'erreur du code.

## Utilisation dans un pipeline

L'import crée normalement un TIFF pyramidal pour chaque image source et conserve son chemin
dans `TileClass::getTiff()`. Un step qui possède une tuile peut ensuite créer un reader avec
ce chemin et demander uniquement la région utile :

```cpp
IONs::PyramidalTiffReader reader(tile.getTiff());
cv::Mat region;
std::string errorMessage;

if (!reader.readRegion(level, requestedRegion, region, errorMessage)) {
    // Le step traite l'erreur selon son contrat.
}
```

Les pixels ne sont pas stockés dans `TileClass`. Le TIFF pyramidal doit donc rester disponible
pendant l'exécution du pipeline et lors d'une reprise à partir d'un checkpoint.
