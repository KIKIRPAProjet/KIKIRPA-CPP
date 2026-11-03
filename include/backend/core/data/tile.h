// include/backend/core/data/tile.h
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace DataNs {

    constexpr int BLOCK_SIZE = 512;   // côté d'un bloc en pixels ; identique à la taille des tuiles du TIFF pyramidal
    constexpr int LEVEL_COUNT = 4;    // niveaux de la pyramide : pleine résolution, puis 1/2, 1/4 et 1/8

    // Un niveau de la pyramide. Les pixels restent sur disque (TIFF tuilé) : seules les dimensions
    // sont gardées en mémoire, la grille de blocs s'en déduit.
    struct Level {
        int width = 0;
        int height = 0;
        int blocksCountX = 0;     // dimensions de la grille de blocs
        int blocksCountY = 0;
        bool greyscale = false;   // niveaux réduits stockés en niveaux de gris
    };

    // Identifiant d'un bloc : tuile, niveau et position dans la grille de blocs. Sert de clé de cache.
    struct BlockId {
        int tileId = 0;
        int level = 0;
        int blockX = 0;
        int blockY = 0;
        bool operator==(const BlockId& other) const {
            return tileId == other.tileId && level == other.level && blockX == other.blockX && blockY == other.blockY;
        }
    };

    // Fiche d'une tuile, gardée en mémoire. Elle ne contient aucun pixel : les algorithmes lisent la zone
    // voulue dans le TIFF pyramidal. Les voisines sont des pointeurs non propriétaires (voir TileList).
    class TileClass {

        public:
            // fullWidth/fullHeight : dimensions de la pleine résolution ; tileType : type OpenCV de cette résolution.
            TileClass(int tileId, std::filesystem::path sourcePath, std::filesystem::path tiffPath, int fullWidth, int fullHeight, int tileType);

            TileClass* getLeft() const { return left; }
            TileClass* getRight() const { return right; }
            TileClass* getTop() const { return top; }
            TileClass* getBottom() const { return bottom; }
            // Un pointeur nul signifie « pas de voisine de ce côté ».
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

            // Dimensions du niveau `level` (0 = pleine résolution). Lève std::out_of_range si le niveau n'existe pas.
            const Level& getLevel(int level) const { return pyramid.at(level); }

            // Bloc contenant le pixel (pixelX, pixelY), exprimé dans les coordonnées du niveau `level`.
            BlockId getBlockAt(int level, int pixelX, int pixelY) const { return {id, level, pixelX / BLOCK_SIZE, pixelY / BLOCK_SIZE}; }

        private:
            int id = 0;                     // identifiant unique, utilisé par PairMatches et les checkpoints
            std::filesystem::path source;   // image d'origine, jamais modifiée
            std::filesystem::path tiff;     // TIFF tuilé pyramidal créé à l'import
            int type = 0;                   // type OpenCV de la pleine résolution (ex. CV_16UC3)

            // Voisines dans la grille de tuiles, nulles si absentes. Non propriétaires : valides tant que la TileList vit.
            TileClass* left = nullptr;
            TileClass* right = nullptr;
            TileClass* top = nullptr;
            TileClass* bottom = nullptr;
            std::array<Level, LEVEL_COUNT> pyramid;   // dimensions de chaque niveau, calculées par le constructeur

    };

    // Liste des tuiles (type de donnée "Tiles"), créée par le step import. shared_ptr : les copies du DataStoreClass
    // partagent les mêmes TileClass, donc les pointeurs vers les voisines restent valides.
    using TileList = std::vector<std::shared_ptr<TileClass>>;

}
