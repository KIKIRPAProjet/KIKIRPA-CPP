#pragma once

#include "Niveau.hpp"

#include <string>
#include <vector>
#include <stdexcept>
#include <utility>

/**
 * @brief Represents the multiresolution image pyramid associated with a tile.
 *
 * Each Pyramide references a tiled, multiresolution TIFF file stored on disk.
 * Original image files are never modified. It manages an ordered collection of
 * Niveau objects, ranging from level 0 (full resolution 1:1) down to coarser
 * resolutions (e.g. 1/2, 1/4, 1/8).
 */
class Pyramide {
public:
    /**
     * @brief Construct an empty Pyramide.
     * @param tiffFilePath Path to the tiled multiresolution TIFF file on disk.
     * @param originalWidth Full resolution image width in pixels.
     * @param originalHeight Full resolution image height in pixels.
     * @param defaultBlockSize Default block edge size in pixels (default 512).
     */
    Pyramide(std::string tiffFilePath,
             int originalWidth,
             int originalHeight,
             int defaultBlockSize = 512)
        : tiffFilePath_(std::move(tiffFilePath)),
          originalWidth_(originalWidth),
          originalHeight_(originalHeight),
          defaultBlockSize_(defaultBlockSize) {}

    // --- Accessors ---

    const std::string& getFilePath() const noexcept { return tiffFilePath_; }
    void setFilePath(std::string path) { tiffFilePath_ = std::move(path); }

    int getOriginalWidth() const noexcept { return originalWidth_; }
    int getOriginalHeight() const noexcept { return originalHeight_; }
    int getDefaultBlockSize() const noexcept { return defaultBlockSize_; }

    std::size_t getLevelCount() const noexcept { return levels_.size(); }
    const std::vector<Niveau>& getLevels() const noexcept { return levels_; }

    /**
     * @brief Retrieve a specific resolution level by index (const).
     * @throws std::out_of_range if index is invalid.
     */
    const Niveau& getLevel(std::size_t index) const {
        if (index >= levels_.size()) {
            throw std::out_of_range("Pyramid level index out of range");
        }
        return levels_[index];
    }

    /**
     * @brief Retrieve a specific resolution level by index (mutable).
     * @throws std::out_of_range if index is invalid.
     */
    Niveau& getLevel(std::size_t index) {
        if (index >= levels_.size()) {
            throw std::out_of_range("Pyramid level index out of range");
        }
        return levels_[index];
    }

    /**
     * @brief Add a resolution level to the pyramid.
     */
    void addLevel(const Niveau& level) {
        levels_.push_back(level);
    }

    /**
     * @brief Add a resolution level by moving.
     */
    void addLevel(Niveau&& level) {
        levels_.push_back(std::move(level));
    }

    // --- Factory Helper ---

    /**
     * @brief Utility factory method to construct a standard multi-resolution pyramid
     *        with dyadic downsampling (1/1, 1/2, 1/4, 1/8, ...).
     * @param filePath Path to the tiled TIFF on disk.
     * @param width Original image width.
     * @param height Original image height.
     * @param numLevels Number of resolution levels to generate (e.g. 4).
     * @param blockSize Block size (normally 512).
     * @return Fully configured Pyramide object.
     */
    static Pyramide createStandardPyramid(const std::string& filePath,
                                          int width,
                                          int height,
                                          std::size_t numLevels = 4,
                                          int blockSize = 512) {
        Pyramide pyramid(filePath, width, height, blockSize);

        int curWidth = width;
        int curHeight = height;
        double scale = 1.0;

        for (std::size_t i = 0; i < numLevels; ++i) {
            pyramid.addLevel(Niveau(
                static_cast<int>(i),
                scale,
                curWidth,
                curHeight,
                blockSize,
                blockSize
            ));

            // Halve dimensions for next level, maintaining minimum of 1 pixel
            curWidth = std::max(1, curWidth / 2);
            curHeight = std::max(1, curHeight / 2);
            scale /= 2.0;
        }

        return pyramid;
    }

private:
    std::string tiffFilePath_;
    int originalWidth_{0};
    int originalHeight_{0};
    int defaultBlockSize_{512};
    std::vector<Niveau> levels_;
};
