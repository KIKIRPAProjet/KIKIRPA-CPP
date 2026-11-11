# TIFF pyramidal : écriture et lecture

Code :

- `core/io/pyramidal_tiff_writer.h` / `.cpp` : création d'un TIFF pyramidal tuilé ;
- `core/io/pyramidal_tiff_reader.h` / `.cpp` : lecture d'une région d'un niveau de la pyramide
  et récupération de métadonnées TIFF optionnelles.

Tests : `tests/core/test_pyramidal_tiff_reader.cpp`.

Pour écrire une image trop grande pour la mémoire (la mosaïque), bloc par bloc et sans pyramide : `IONs::TiledTiffWriterClass`, voir [TiledTiffWriter.md](TiledTiffWriter.md). Le reader décrit ici relit aussi ses fichiers (niveau `0`).

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
pixels et une compression LZW. Les dimensions des niveaux réduits sont calculées par division
entière : elles peuvent donc être arrondies vers le bas.

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

`IONs::PyramidalTiffReaderClass` est construit avec le chemin d'un TIFF pyramidal. `readRegion` reçoit :

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

IONs::PyramidalTiffReaderClass reader("output/input_pyramid.tif");
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

La région doit avoir des coordonnées non négatives et une largeur et une hauteur strictement
positives. Elle doit être entièrement contenue dans le niveau demandé : le reader ne la
tronque pas automatiquement. En cas de succès, `output` a exactement la taille de `region`
et le type du niveau lu.

`readPyramidDimensions` permet de connaître les dimensions réellement stockées, dans l'ordre
du niveau `0` aux niveaux réduits.

## Lire la région correspondant à une vue et un zoom

`readViewport` est une fonction backend destinée à une future vue interactive. Elle reçoit :

| Paramètre | Signification |
|---|---|
| `origin` | coin supérieur gauche visible, en pixels de l'image pleine résolution |
| `viewportSize` | largeur et hauteur disponibles pour afficher l'image, en pixels écran |
| `zoomPercent` | facteur de zoom d'affichage : `100` signifie 1 pixel écran par pixel source |

Ce pourcentage est bien un **pourcentage de zoom**, pas un pourcentage de qualité. Par exemple,
à `25 %`, une vue de 1 200 × 800 pixels couvre environ 4 800 × 3 200 pixels de l'image d'origine.
Le backend choisit le niveau dont la résolution est la plus appropriée à ce facteur, convertit
la zone visible vers les coordonnées du niveau et la limite aux dimensions de l'image.

Le résultat `PyramidViewportRegion` fournit le niveau lu, le rectangle transmis à `readRegion`
(`region`, dans les coordonnées de ce niveau), le rectangle équivalent en coordonnées pleine
résolution (`fullResolutionRegion`) et les pixels lus (`pixels`). Le frontend peut dessiner
`pixels` à la position `fullResolutionRegion` de son image, en appliquant son facteur de zoom.
À `100 %`, le niveau `0` est choisi et le rectangle correspond directement aux pixels originaux.

Exemple : avec `origin = (5000, 3000)`, une zone écran de 1 200 × 800 et un zoom de `100 %`,
le lecteur demande le rectangle `(5000, 3000, 1200, 800)` au niveau `0`, s'il est entièrement
dans l'image. Si une partie dépasse un bord, elle est rognée. Un viewport sans intersection
avec l'image ou des paramètres invalides renvoient `false` avec un message d'erreur.

Le frontend doit appeler cette fonction en dehors du fil graphique si la lecture peut être
lente, puis appliquer le résultat uniquement s'il correspond toujours à la dernière position
et au dernier zoom demandés. Cette vérification protège la vue contre les lectures asynchrones
qui se terminent dans un ordre différent des déplacements.

Exemple de calcul côté vue : convertir le rectangle pleine résolution retourné en coordonnées
locales du viewport, puis appliquer le zoom à son dessin :

```cpp
const double zoom = zoomPercent / 100.0;
IONs::PyramidViewportRegion result;
std::string errorMessage;

if (reader.readViewport(
        IONs::PyramidViewportRequest{
            originInFullResolution,
            cv::Size(viewportWidth, viewportHeight),
            zoomPercent},
        result,
        errorMessage)) {
    const cv::Rect2d& sourceRect = result.fullResolutionRegion;
    const QRectF target(
        (sourceRect.x - originInFullResolution.x) * zoom,
        (sourceRect.y - originInFullResolution.y) * zoom,
        sourceRect.width * zoom,
        sourceRect.height * zoom);
    // Convert result.pixels to QImage and draw it into target.
}
```

Le calcul d'un identifiant de requête reste à la charge de la vue : incrémenter cet identifiant
pour chaque nouvelle demande et ignorer un résultat asynchrone si son identifiant n'est plus
celui de la vue courante.

Dans l'application Qt, le canevas du mode Export propose « Ouvrir mosaïque TIFF… ». Après
ouverture, « Ajuster » affiche l'ensemble, les boutons ou la molette changent le zoom et un
glisser-déposer déplace l'image. Chaque nouvelle vue utilise `readViewport` en tâche de fond;
le résultat est dessiné à sa position pleine résolution et seules les réponses de la dernière
demande sont appliquées. Pour que la vue d'ensemble utilise un niveau réduit, le fichier ouvert
doit contenir une pyramide. Un TIFF à un seul niveau reste lisible, mais n'offre pas de niveau
réduit distinct.

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

## Lire les métadonnées optionnelles

`readMetadata` ouvre le TIFF associé au reader et récupère, dans le répertoire principal :

- le profil ICC, copié dans `iccProfile` lorsqu'il est présent ;
- les résolutions TIFF horizontale et verticale, écrites dans `scaleX` et `scaleY`
  lorsqu'elles sont présentes.

Les métadonnées sont optionnelles. Avant la lecture, `iccProfile` est vidé et `scaleX` et
`scaleY` sont initialisées à `1.0`. L'absence de profil ICC ou de résolution ne constitue
donc pas une erreur : la fonction renvoie `true` et conserve ces valeurs par défaut.
Le profil ICC est copié dans le vecteur de sortie, qui reste indépendant des données gérées
par libtiff après la fermeture du fichier.

```cpp
#include "core/io/pyramidal_tiff_reader.h"

#include <cstdint>
#include <string>
#include <vector>

IONs::PyramidalTiffReaderClass reader("output/input_pyramid.tif");
std::vector<std::uint8_t> iccProfile;
double scaleX = 0.0;
double scaleY = 0.0;
std::string errorMessage;

const bool success = reader.readMetadata(
    iccProfile,
    scaleX,
    scaleY,
    errorMessage
);
```

Cette fonction ne valide pas le contenu de l'image et ne sélectionne pas un niveau
particulier de la pyramide. Elle renvoie `false` uniquement si le TIFF ne peut pas être
ouvert ; dans ce cas, `errorMessage` décrit l'erreur.

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

`readMetadata` renvoie `false` si le fichier ne peut pas être ouvert. Les métadonnées
optionnelles absentes ne provoquent pas d'échec.

## Utilisation dans un pipeline

L'import crée normalement un TIFF pyramidal pour chaque image source et conserve son chemin
dans `TileClass::getTiff()`. Un step qui possède une tuile peut ensuite créer un reader avec
ce chemin et demander uniquement la région utile :

```cpp
IONs::PyramidalTiffReaderClass reader(tile.getTiff());
cv::Mat region;
std::string errorMessage;

if (!reader.readRegion(level, requestedRegion, region, errorMessage)) {
    // Le step traite l'erreur selon son contrat.
}
```

Les pixels ne sont pas stockés dans `TileClass`. Le TIFF pyramidal doit donc rester disponible
pendant l'exécution du pipeline et lors d'une reprise à partir d'un checkpoint.
