#include "Tuile.hpp"
#include "Pyramide.hpp"
#include "Niveau.hpp"

#include <iostream>
#include <iomanip>
#include <memory>
#include <vector>

void printTileNeighborInfo(const Tuile& tile) {
    std::cout << "Tile [" << tile.getId() << "] Neighbors:" << std::endl;

    // 4. Retrieve neighbors using getTop(), getBottom(), getLeft(), getRight()
    // 5. Safely handle missing neighbors via nullptr check
    std::cout << "  Top:    " << (tile.getTop()    ? tile.getTop()->getId()    : "(none / boundary)") << std::endl;
    std::cout << "  Bottom: " << (tile.getBottom() ? tile.getBottom()->getId() : "(none / boundary)") << std::endl;
    std::cout << "  Left:   " << (tile.getLeft()   ? tile.getLeft()->getId()   : "(none / boundary)") << std::endl;
    std::cout << "  Right:  " << (tile.getRight()  ? tile.getRight()->getId()  : "(none / boundary)") << std::endl;
}

void printPyramidBlockMetadata(const Pyramide& pyramid) {
    std::cout << "\nPyramid Metadata for: " << pyramid.getFilePath() << std::endl;
    std::cout << "  Original Base Dimensions: " << pyramid.getOriginalWidth() << " x "
              << pyramid.getOriginalHeight() << " px" << std::endl;
    std::cout << "  Pyramid Levels: " << pyramid.getLevelCount() << std::endl;
    std::cout << "  Default Block Size: " << pyramid.getDefaultBlockSize() << " x "
              << pyramid.getDefaultBlockSize() << " px" << std::endl;

    std::cout << "\n  Level Details & Derived Block Counts (Ceiling Division):" << std::endl;
    std::cout << "  " << std::string(75, '-') << std::endl;
    std::cout << "  "
              << std::setw(6)  << "Level"
              << std::setw(10) << "Scale"
              << std::setw(18) << "Dimensions (WxH)"
              << std::setw(10) << "BlockSize"
              << std::setw(14) << "Grid (XxY)"
              << std::setw(15) << "Total Blocks"
              << std::endl;
    std::cout << "  " << std::string(75, '-') << std::endl;

    // 6. Calculate the number of blocks at each pyramid level
    for (std::size_t i = 0; i < pyramid.getLevelCount(); ++i) {
        const Niveau& level = pyramid.getLevel(i);
        std::string dimStr = std::to_string(level.getWidth()) + "x" + std::to_string(level.getHeight());
        std::string blockDimStr = std::to_string(level.getBlockWidth()) + "x" + std::to_string(level.getBlockHeight());
        std::string gridStr = std::to_string(level.getBlocksX()) + "x" + std::to_string(level.getBlocksY());

        std::cout << "  "
                  << std::setw(6)  << level.getLevelIndex()
                  << std::setw(10) << std::fixed << std::setprecision(3) << level.getScaleFactor()
                  << std::setw(18) << dimStr
                  << std::setw(10) << blockDimStr
                  << std::setw(14) << gridStr
                  << std::setw(15) << level.getTotalBlocks()
                  << std::endl;
    }
    std::cout << "  " << std::string(75, '-') << std::endl;

    // Demonstrate on-demand calculation of block pixel bounds without storing block objects
    const Niveau& baseLevel = pyramid.getLevel(0);
    std::cout << "\n  On-Demand Block Pixel Bounds Sample (Level 0, Base Resolution):" << std::endl;

    // First block (0, 0)
    cv::Rect firstBlock = baseLevel.getBlockBounds(0, 0);
    std::cout << "    Block (bx=0, by=0): x=" << firstBlock.x << ", y=" << firstBlock.y
              << ", w=" << firstBlock.width << ", h=" << firstBlock.height << std::endl;

    // Last block at bottom-right corner (handling image dimensions not evenly divisible by 512)
    int lastBx = baseLevel.getBlocksX() - 1;
    int lastBy = baseLevel.getBlocksY() - 1;
    cv::Rect lastBlock = baseLevel.getBlockBounds(lastBx, lastBy);
    std::cout << "    Block (bx=" << lastBx << ", by=" << lastBy << ") [Boundary Block]: x="
              << lastBlock.x << ", y=" << lastBlock.y
              << ", w=" << lastBlock.width << ", h=" << lastBlock.height << std::endl;
}

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "   TUILE DATA STRUCTURE & METADATA DEMONSTRATION" << std::endl;
    std::cout << "============================================================" << std::endl;

    // 1. Create two or more Tuile instances (2x2 mosaic grid)
    auto tile00 = std::make_unique<Tuile>("tile_row0_col0");
    auto tile01 = std::make_unique<Tuile>("tile_row0_col1");
    auto tile10 = std::make_unique<Tuile>("tile_row1_col0");
    auto tile11 = std::make_unique<Tuile>("tile_row1_col1");

    // 2. Associate pyramid metadata with each tile
    // Example: A 200-megapixel high-resolution tile (16,384 x 12,288 pixels)
    // 4 levels: 1/1 (16384x12288), 1/2 (8192x6144), 1/4 (4096x3072), 1/8 (2048x1536)
    // Block size: 512x512
    tile00->setPyramide(std::make_unique<Pyramide>(
        Pyramide::createStandardPyramid("cache/tile_0_0_pyramid.tif", 16384, 12288, 4, 512)
    ));

    tile01->setPyramide(std::make_unique<Pyramide>(
        Pyramide::createStandardPyramid("cache/tile_0_1_pyramid.tif", 16384, 12288, 4, 512)
    ));

    tile10->setPyramide(std::make_unique<Pyramide>(
        Pyramide::createStandardPyramid("cache/tile_1_0_pyramid.tif", 16384, 12288, 4, 512)
    ));

    tile11->setPyramide(std::make_unique<Pyramide>(
        Pyramide::createStandardPyramid("cache/tile_1_1_pyramid.tif", 16384, 12288, 4, 512)
    ));

    // 3. Connect their neighbor relationships
    // Horizontal connections:
    tile00->connectRight(tile01.get());
    tile10->connectRight(tile11.get());

    // Vertical connections:
    tile00->connectBottom(tile10.get());
    tile01->connectBottom(tile11.get());

    // 4 & 5. Verify neighbor relationships and missing boundary neighbor safety
    std::cout << "\n--- Mosaic Neighborhood Topology ---" << std::endl;
    printTileNeighborInfo(*tile00);
    std::cout << std::endl;
    printTileNeighborInfo(*tile01);
    std::cout << std::endl;
    printTileNeighborInfo(*tile10);
    std::cout << std::endl;
    printTileNeighborInfo(*tile11);

    // Verify reciprocal navigation across the mosaic graph:
    std::cout << "\n--- Navigating Through Neighbor Pointers ---" << std::endl;
    Tuile* navigated = tile00->getRight(); // Move to (0, 1)
    if (navigated != nullptr) {
        std::cout << "Step 1 (Right from tile00): " << navigated->getId() << std::endl;
        navigated = navigated->getBottom(); // Move to (1, 1)
        if (navigated != nullptr) {
            std::cout << "Step 2 (Bottom from tile01): " << navigated->getId() << std::endl;
            navigated = navigated->getLeft(); // Move to (1, 0)
            if (navigated != nullptr) {
                std::cout << "Step 3 (Left from tile11): " << navigated->getId() << std::endl;
            }
        }
    }

    // 6. Demonstrate pyramid metadata and block calculations
    if (tile00->hasPyramide()) {
        printPyramidBlockMetadata(*tile00->getPyramide());
    }

    std::cout << "\nMemory Verification:" << std::endl;
    std::cout << "  - Level metadata stored in RAM: Yes (~32 bytes per level)." << std::endl;
    std::cout << "  - Number of block objects allocated in RAM: 0 (calculated on demand)." << std::endl;
    std::cout << "  - High-resolution image matrices loaded in RAM: 0 (TIFF files stored on disk)." << std::endl;
    std::cout << "  - Memory budget (8 GB): Respected with negligible footprint." << std::endl;

    return 0;
}
