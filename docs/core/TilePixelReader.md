# Lire les pixels d'une tuile : cache ou image de l'œuvre

Code : `core/io/tile_pixel_reader.h` / `.cpp`. Tests : `tests/core/test_tile_pixel_reader.cpp`.

## Pourquoi

Le TIFF pyramidal du cache (`core/io/pyramid_cache.h`) contenait quatre niveaux, dont le niveau 0 : une **copie** de
l'image de l'œuvre, qui pesait à elle seule **73 %** du cache. Mesuré sur `data/02-Philadelphia` (6 tuiles de
11 600 × 8 700, RGB 16 bits) : **5,4 Gio de cache**, dont 4,4 de copie.

Depuis le 24/11/2026, le niveau 0 reste dans l'image de l'œuvre quand celle-ci sait servir une région de sa pleine
résolution. Même jeu de données, même import : **962 Mio**, soit **−82 %** (160 Mio par tuile au lieu de 919).

## La sonde : `probeSource`

Elle lit les seuls tags du fichier source et répond à une question : peut-on y lire une région **sans décoder toute
l'image** ?

| Source | Réponse | Pourquoi |
|---|---|---|
| TIFF tuilé | oui | une tuile suffit |
| TIFF à bandes **non compressé**, canaux entrelacés, ordre des octets de la machine | oui | l'octet d'une ligne se calcule (`StripOffsets`) |
| TIFF à bandes de **256 lignes au plus** | oui | les bandes recoupées sont décodées |
| TIFF à **une seule bande compressée** | non | chaque région coûterait l'image entière |
| canaux séparés, palette, YCbCr, CMJN | non | les canaux ne sont pas utilisables tels quels |
| pas un TIFF lisible (JPEG, PNG) | non | libtiff ne l'ouvre pas |

« Oui » implique que `PyramidalTiffReaderClass` sait lire ce fichier : profondeur, nombre de canaux et format
d'échantillon sont vérifiés en même temps. La sonde ne lève jamais.

Les deux jeux réels répondent oui : `02-Philadelphia` est non compressé (une bande de 8 700 lignes), et `01-Nominal`
est en LZW avec 25 lignes par bande.

## `TilePixelReaderClass`

Elle donne les mêmes méthodes que `PyramidalTiffReaderClass` (`readRegion`, `readLevel`, `getLevelDimensions`,
`getLevelCount`) et **numérote les niveaux comme ceux de la pyramide** : 0 = pleine résolution, 1 = moitié, etc.,
que le fichier de cache les contienne tous ou non. Elle lit le niveau 0 dans l'image de l'œuvre ou dans le cache, et
décale d'un les niveaux réduits quand le cache commence au niveau 1.

C'est sa seule raison d'être, et c'est pour ça que **tout code qui compte des niveaux à la main doit passer par
elle** : `fusion`, et les trois aperçus de l'interface (`readPyramidalPreview`, `drawTileOnCanvas`,
`renderFallbackPreview`).

```cpp
#include "core/io/tile_pixel_reader.h"

const IONs::TilePixelReaderClass reader(tile.getSource(), tile.getTiff());
cv::Mat region;
std::string errorMessage;
reader.readRegion(0, cv::Rect(100, 50, 400, 300), region, errorMessage);   // la pleine résolution, où qu'elle soit
```

Un second constructeur prend la réponse déjà connue (`levelZeroInSource`), pour ne pas rouvrir le cache à chaque
région : `fusion` décide une fois par tuile, puis lit bloc par bloc.

## Où la mise en page est écrite : dans le fichier

La question « ce cache contient-il son niveau 0 ? » se lit dans **le nombre de niveaux du fichier** : `LEVEL_COUNT`
s'il le contient, un de moins sinon. Elle n'est **pas** écrite dans la provenance.

C'est un choix, et il a été payé une fois : la première version la lisait dans la provenance, et le test
`ImportAndImmediatePipelineRunConcurrentExecution` a échoué aussitôt. Ce test retire la provenance en boucle pendant
qu'un pipeline tourne (une réécriture concurrente, ce qui arrive vraiment entre l'interface et la ligne de commande).
Provenance absente, la réponse devenait « le cache contient son niveau 0 » — et les niveaux réduits étaient lus comme
la pleine résolution. Des pixels à la mauvaise échelle, **sans aucune erreur**.

D'où la règle : la mise en page d'un fichier se lit dans le fichier. Changer `DataNs::LEVEL_COUNT` oblige donc à
incrémenter `PYRAMID_FORMAT_VERSION`, ce qui réécrit les caches.

Et quand le cache n'est **pas lisible** à cet instant — un remplacement en cours dans un autre fil, ou un fichier
absent — `getLevelCount()` rend 0, et la décision retombe sur la sonde de la source, qui est précisément ce qui a
décidé de la mise en page à l'écriture. **Jamais** sur « le cache contient son niveau 0 » : ce défaut-là ferait
lire les niveaux réduits comme la pleine résolution. C'est ce que faisait la première version, et c'est ce qui a
fait échouer le test de concurrence une fois sur deux.

## Un niveau entier : `readTileLevel`

`readTileLevel(sourcePath, pyramidPath, level)` rend **tous** les pixels d'un niveau, **en RGB**, pris au meilleur
endroit : dans le cache quand la tuile en a un et qu'il contient le niveau demandé (au niveau 0 un cache réduit n'a rien à servir, et les pixels viennent alors de l'image de l'œuvre), sinon dans l'image de l'œuvre, réduite autant de fois que
nécessaire. C'est ce qu'appellent les steps qui travaillent sur un niveau entier plutôt que sur des régions :
`phase-correlation` (par `readPixels`), `sift` et `lightglue` (par `matching_step_utils::readImage`),
`overlap-estimation`, `intensity-correction` et `dynamic-programming-dijkstra`.

Le gain est proportionnel au niveau :

| Niveau | Lu avant (source + réductions) | Lu maintenant | Qui |
|---|---|---|---|
| 0 | 578 Mio | 578 Mio | `sift`, `lightglue`, `overlap-estimation`, `dp-dijkstra` |
| 1 | 578 Mio + une réduction | 145 Mio | `phase-correlation` avec `niveau` = 1 |
| 3 | 578 Mio + trois réductions | ~10 Mio | `intensity-correction` |

Le repli sans cache donne **exactement les mêmes pixels** : réduire puis échanger les canaux, ou l'inverse, revient
au même, `INTER_AREA` moyennant canal par canal. Un test le vérifie niveau par niveau.

Et comme tout arrive désormais en RGB, les conversions en gris des algorithmes sont nommées `RGB2GRAY` au lieu de
`BGR2GRAY` (même luminance, seul le nom était faux), et les deux `COLOR_BGR2RGB` du step
`dynamic-programming-dijkstra` ont disparu.

**Ce que ça ne fait pas** : lire seulement la bande de recouvrement. L'estimateur découpe la bande nominale
lui-même et `computeCommonAreas` travaille dans le repère de la tuile entière : il faudrait que les algorithmes
reçoivent une région **et son origine**, donc changer leur contrat. C'est un chantier à part.

## Ce qui reste dans le cache

Les niveaux réduits, toujours : ils n'existent nulle part ailleurs. Et le niveau 0 pour les sources de la colonne
« non » du tableau — le chemin classique reste la référence, la lecture directe n'est qu'un raccourci.
