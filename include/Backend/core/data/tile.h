// include/Backend/core/data/tile.h
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace DataNs {

    constexpr int BLOCK_SIZE = 512;   // blocs de 512 x 512 pixels (taille à discuter)
    constexpr int LEVEL_COUNT = 4;     // 1, 1/2, 1/4, 1/8

    // Un niveau de la pyramide. Les blocs restent sur disque (TIFF tuilé) : seules
    // les dimensions sont gardées en RAM, la grille de blocs s'en déduit.
    struct Level {
        int width = 0;
        int height = 0;
        int blocksCountX = 0;
        int blocksCountY = 0;   // nombre de blocs en x et en y
        bool greyscale = false; // niveaux réduits stockés en niveaux de gris
    };

    // Identifiant d'un bloc : (tuile, niveau, bx, by), clé du cache
    struct BlockId {
        int tileId = 0;
        int level = 0;
        int blockX = 0;
        int blockY = 0;
        bool operator==(const BlockId& other) const {
            return tileId == other.tileId && level == other.level && blockX == other.blockX && blockY == other.blockY;
        }
    };

    // Fiche d'une tuile, gardée en RAM. Aucun pixel ici : les algorithmes passent
    // par readRegion(tuile, niveau, rectangle).
    class TileClass {

        public:
            TileClass(int tileId, std::filesystem::path sourcePath, std::filesystem::path tiffPath, int fullWidth, int fullHeight, int tileType);

            TileClass* getLeft() const { return left; }
            TileClass* getRight() const { return right; }
            TileClass* getTop() const { return top; }
            TileClass* getBottom() const { return bottom; }
            void setNeighbours(TileClass* leftTile, TileClass* rightTile, TileClass* topTile, TileClass* bottomTile) {
                left = leftTile;
                right = rightTile;
                top = topTile;
                bottom = bottomTile;
            }

            int getId() const { return id; }
            const std::filesystem::path& getSource() const { return source; }
            const std::filesystem::path& getTiff() const { return tiff; }
            int getType() const { return type; }

            const Level& getLevel(int level) const { return pyramid.at(level); }

            // Bloc contenant le pixel (x, y) du niveau n
            BlockId getBlockAt(int level, int pixelX, int pixelY) const { return {id, level, pixelX / BLOCK_SIZE, pixelY / BLOCK_SIZE}; }

        private:
            int id = 0;
            std::filesystem::path source;   // fichier d'origine, jamais modifié
            std::filesystem::path tiff;     // TIFF tuilé pyramidal créé à l'import
            int type = 0;                   // type OpenCV de la pleine résolution (ex. CV_16UC3)

            TileClass* left = nullptr;
            TileClass* right = nullptr;
            TileClass* top = nullptr;
            TileClass* bottom = nullptr;
            std::array<Level, LEVEL_COUNT> pyramid;

    };

    // PipelineOption "Tiles" : les fiches créées par le step import.
    // shared_ptr : les copies (DataStoreClass) gardent les mêmes Tuile, donc les voisins restent valides.
    using TileList = std::vector<std::shared_ptr<TileClass>>;

}
