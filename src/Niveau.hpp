#pragma once

#include <cstddef>
#include <algorithm>
#include <utility>

// OpenCV compatibility: Use actual OpenCV headers if available, or lightweight fallbacks.
#if __has_include(<opencv2/core.hpp>)
#include <opencv2/core.hpp>
#else
namespace cv {
    class Mat {
    public:
        int rows{0};
        int cols{0};
        Mat() = default;
        Mat(int r, int c) : rows(r), cols(c) {}
        bool empty() const { return rows == 0 || cols == 0; }
    };

    struct Rect {
        int x{0};
        int y{0};
        int width{0};
        int height{0};

        Rect() = default;
        Rect(int _x, int _y, int _w, int _h) : x(_x), y(_y), width(_w), height(_h) {}
    };
}
#endif

/**
 * @brief Represents a single resolution level in the image pyramid.
 *
 * Each Niveau stores geometric metadata for a specific downsampling tier:
 * - Dimensions in pixels (width, height)
 * - Downsampling scale factor (e.g. 1.0 for 1/1, 0.5 for 1/2, 0.25 for 1/4)
 * - Block dimensions (typically 512x512 pixels)
 *
 * In accordance with memory constraints (8 GB RAM limit), Niveau does NOT
 * store in-memory objects for each block. Block counts and pixel bounds are
 * calculated dynamically on demand using ceiling division.
 */
class Niveau {
public:
    /**
     * @brief Construct a new Niveau metadata object.
     * @param levelIndex Index of the level (0 = full resolution, 1 = 1/2, etc.).
     * @param scaleFactor Scale factor relative to the base image (e.g. 1.0, 0.5, 0.25).
     * @param width Width in pixels at this pyramid level.
     * @param height Height in pixels at this pyramid level.
     * @param blockWidth Width of a tile block in pixels (default 512).
     * @param blockHeight Height of a tile block in pixels (default 512).
     */
    Niveau(int levelIndex,
           double scaleFactor,
           int width,
           int height,
           int blockWidth = 512,
           int blockHeight = 512)
        : levelIndex_(levelIndex),
          scaleFactor_(scaleFactor),
          width_(width),
          height_(height),
          blockWidth_(blockWidth),
          blockHeight_(blockHeight) {}

    // --- Dimension and level accessors ---

    int getLevelIndex() const noexcept { return levelIndex_; }
    double getScaleFactor() const noexcept { return scaleFactor_; }
    int getWidth() const noexcept { return width_; }
    int getHeight() const noexcept { return height_; }
    int getBlockWidth() const noexcept { return blockWidth_; }
    int getBlockHeight() const noexcept { return blockHeight_; }

    // --- Block grid derivations (calculated on demand, not stored) ---

    /**
     * @brief Calculate the number of blocks along the X axis using ceiling division:
     *        blocksX = (width + blockWidth - 1) / blockWidth
     */
    int getBlocksX() const noexcept {
        if (width_ <= 0 || blockWidth_ <= 0) return 0;
        return (width_ + blockWidth_ - 1) / blockWidth_;
    }

    /**
     * @brief Calculate the number of blocks along the Y axis using ceiling division:
     *        blocksY = (height + blockHeight - 1) / blockHeight
     */
    int getBlocksY() const noexcept {
        if (height_ <= 0 || blockHeight_ <= 0) return 0;
        return (height_ + blockHeight_ - 1) / blockHeight_;
    }

    /**
     * @brief Total number of blocks at this resolution level.
     */
    std::size_t getTotalBlocks() const noexcept {
        return static_cast<std::size_t>(getBlocksX()) * static_cast<std::size_t>(getBlocksY());
    }

    /**
     * @brief Derive the pixel bounding box for a given block index without storing a block object.
     * @param bx 0-indexed column index of the block (0 <= bx < getBlocksX()).
     * @param by 0-indexed row index of the block (0 <= by < getBlocksY()).
     * @return cv::Rect containing (x, y, width, height) of the block in level coordinates.
     */
    cv::Rect getBlockBounds(int bx, int by) const noexcept {
        int x = bx * blockWidth_;
        int y = by * blockHeight_;
        int w = std::min(blockWidth_, width_ - x);
        int h = std::min(blockHeight_, height_ - y);
        return cv::Rect(x, y, std::max(0, w), std::max(0, h));
    }

    // --- Optional preview / thumbnail cache ---

    /**
     * @brief Check whether this level currently holds in-memory cached image data
     *        (typically only utilized for very low-resolution overview levels).
     */
    bool hasCachedData() const noexcept {
        return !cachedData_.empty();
    }

    /**
     * @brief Access the in-memory cached image matrix, if available.
     */
    const cv::Mat& getCachedData() const noexcept {
        return cachedData_;
    }

    /**
     * @brief Store an in-memory preview matrix for this level (e.g. low-res thumbnail).
     *        Full-resolution levels should remain empty to preserve RAM.
     */
    void setCachedData(cv::Mat data) {
        cachedData_ = std::move(data);
    }

    /**
     * @brief Clear in-memory cached matrix to free RAM.
     */
    void clearCachedData() {
        cachedData_ = cv::Mat();
    }

private:
    int levelIndex_{0};
    double scaleFactor_{1.0};
    int width_{0};
    int height_{0};
    int blockWidth_{512};
    int blockHeight_{512};

    // Kept empty for high-resolution levels to respect the 8 GB RAM budget.
    cv::Mat cachedData_{};
};
