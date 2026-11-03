// src/backend/core/data/tile.cpp
#include "core/data/tile.h"

#include <utility>

namespace DataNs {

    // Construit la fiche et calcule la pyramide. Aucun pixel n'est lu : tout se déduit des dimensions de la pleine résolution.
    TileClass::TileClass(int tileId, std::filesystem::path sourcePath, std::filesystem::path tiffPath, int fullWidth, int fullHeight, int tileType)
        : id(tileId), source(std::move(sourcePath)), tiff(std::move(tiffPath)), type(tileType) {
        for (int levelIndex = 0; levelIndex < LEVEL_COUNT; ++levelIndex) {
            Level& level = pyramid[levelIndex];
            // Décaler de levelIndex bits = diviser par 2^levelIndex, arrondi à l'inférieur : chaque niveau a la moitié des
            // dimensions du précédent. Ce calcul doit rester celui du TIFF écrit par pyramidal_tiff_writer.cpp (cv::resize).
            level.width = fullWidth >> levelIndex;
            level.height = fullHeight >> levelIndex;
            // Nombre de blocs nécessaires pour couvrir le niveau : division arrondie au supérieur, le dernier bloc pouvant être partiel.
            level.blocksCountX = (level.width + BLOCK_SIZE - 1) / BLOCK_SIZE;
            level.blocksCountY = (level.height + BLOCK_SIZE - 1) / BLOCK_SIZE;
            level.greyscale = levelIndex > 0;   // la pleine résolution garde ses canaux : conversion à la volée si besoin
        }
    }

}
