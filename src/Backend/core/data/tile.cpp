// src/Backend/core/data/tile.cpp
#include "core/data/tile.h"

#include <utility>

namespace DataNs {

    TileClass::TileClass(int tileId, std::filesystem::path sourcePath, std::filesystem::path tiffPath, int fullWidth, int fullHeight, int tileType)
        : id(tileId), source(std::move(sourcePath)), tiff(std::move(tiffPath)), type(tileType) {
        for (int levelIndex = 0; levelIndex < LEVEL_COUNT; ++levelIndex) {
            Level& level = pyramid[levelIndex];
            level.width = fullWidth >> levelIndex;   // 1, 1/2, 1/4, 1/8
            level.height = fullHeight >> levelIndex;
            level.blocksCountX = (level.width + BLOCK_SIZE - 1) / BLOCK_SIZE;
            level.blocksCountY = (level.height + BLOCK_SIZE - 1) / BLOCK_SIZE;
            level.greyscale = levelIndex > 0;          // pleine résolution : conversion à la volée
        }
    }

}
