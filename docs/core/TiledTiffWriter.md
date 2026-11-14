# Écrire un TIFF tuilé bloc par bloc

Code : `core/io/tiled_tiff_writer.h` / `.cpp` (`IONs::TiledTiffWriterClass`) et `core/io/file_replacement.h` / `.cpp` (temporaire puis renommage). Tests : `tests/core/test_tiled_tiff_writer.cpp`.

## À quoi il sert

La mosaïque finale ne tient pas en mémoire : 6 octets par pixel en 16 bits RGB, soit 1,8 Go pour 300 millions de pixels ([COMPRENDRE_LE_PROJET.md §6](../COMPRENDRE_LE_PROJET.md#6-la-mémoire--pourquoi-on-en-parle-autant)). `TiledTiffWriterClass` écrit une image **bloc par bloc** : l'assemblage calcule un bloc de 512 × 512 pixels, le donne au writer, puis l'oublie. La mémoire utilisée est celle d'un bloc, quelle que soit la taille de l'image.

C'est le même format que l'ancien writer interne de l'export, dont il est extrait :

- blocs de `DataNs::BLOCK_SIZE` (512) pixels de côté, compressés en LZW ;
- pleine résolution seulement (un seul niveau, pas de pyramide) ;
- TIFF classique, ou **BigTIFF** quand une estimation prudente de la taille (×1,5 pour le pire cas du LZW, plus les tables d'offsets) dépasse 4 Gio.

Pour une image qui tient en mémoire et dont on veut toute la pyramide, il y a `createPyramidalTiledTiff` ([PyramidalTiff.md](PyramidalTiff.md)).

## Utilisation

```cpp
#include "core/io/tiled_tiff_writer.h"

IONs::TiledTiffWriterClass writer;
std::string errorMessage;
if (!writer.open(outputPath, canvasSize, CV_16UC3, errorMessage)) { /* errorMessage */ }
for (const cv::Rect& blockRect : grille) {          // n'importe quel ordre
    const cv::Mat block = calculerBloc(blockRect);  // taille de blockRect, type donné à open()
    if (!writer.writeBlock(blockRect.tl(), block, errorMessage)) { /* errorMessage */ }
}
if (!writer.close(errorMessage)) { /* errorMessage */ }
```

| Méthode | Ce qu'elle fait |
|---|---|
| `open(chemin, taille, type, erreur)` | crée le dossier si besoin et ouvre un fichier **temporaire** à côté de la destination |
| `writeBlock(coin, bloc, erreur)` | écrit un bloc ; `coin` est un multiple de 512, le bloc a la taille du bloc de la grille (512, ou moins sur les bords droit et bas) |
| `close(erreur)` | vérifie que **tous** les blocs de la grille ont été écrits, termine le TIFF, puis le renomme en destination |
| `isBigTiff()` | format choisi à l'ouverture |

Types acceptés : `CV_8U` ou `CV_16U`, avec 1 canal (niveaux de gris) ou 3 canaux (déclarés RGB). Le writer écrit les canaux **dans l'ordre reçu** : c'est à l'appelant de passer de BGR (ordre d'OpenCV) à RGB. L'assemblage s'en sert pour trois fichiers : l'image (`CV_16UC3`), la provenance (`CV_16UC1`) et le masque (`CV_8UC1`).

## Erreurs et fichiers laissés sur le disque

Toutes les méthodes renvoient `false` avec la cause, en anglais, dans `errorMessage`. Cas refusés : type ou taille invalide, writer déjà ouvert (ou pas ouvert), coin non aligné ou hors de l'image, bloc de mauvaise taille ou de mauvais type, bloc déjà écrit, bloc manquant à la fermeture, erreur de libtiff ou du système de fichiers.

La destination n'est remplacée **que par un fichier complet** :

- tant que `close()` n'a pas réussi, seul le temporaire (`<destination>.<horloge>.<numéro>.tmp`) existe ;
- si `close()` échoue, ou si le writer est détruit sans `close()` (exception, retour anticipé), le temporaire est supprimé (RAII) et une destination existante reste intacte ;
- le renommage final remplace la destination d'un coup (`MoveFileExW` sous Windows, `rename` ailleurs).

`IONs::copyFileReplacing` applique la même règle à une copie : l'export s'en sert pour publier `mosaic.tif`.

## Relire

Le fichier produit se relit avec `IONs::PyramidalTiffReaderClass::readRegion(0, région, …)`, qui ne décode que les blocs touchés par la région ([PyramidalTiff.md](PyramidalTiff.md#lire-une-région)).

## Tests

`tests/core/test_tiled_tiff_writer.cpp` : écriture dans le désordre puis relecture exacte pour les trois types utilisés (blocs de bord compris), fermeture refusée s'il manque un bloc (aucun fichier laissé), destruction sans `close()` (l'ancien fichier est gardé, le temporaire supprimé), blocs mal placés, mal dimensionnés ou écrits deux fois, types refusés, et choix du BigTIFF pour une image de 40 000 × 40 000 pixels. Ce dernier test ne vérifie que le choix du format : écrire réellement plus de 4 Gio ne tient pas dans un test unitaire.
