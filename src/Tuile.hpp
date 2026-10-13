#pragma once

#include "Pyramide.hpp"

#include <string>
#include <memory>
#include <utility>

/**
 * @brief Represents an individual image tile within the mosaic image-stitching pipeline.
 *
 * Conforms to the provided UML diagram with 4 directional neighbor pointers:
 * - left, right, top, bottom (non-owning raw pointers to avoid circular ownership)
 * - getLeft(), getRight(), getTop(), getBottom()
 *
 * Each Tuile uniquely owns its multiresolution Pyramide metadata via std::unique_ptr,
 * ensuring clear lifecycle management and preventing memory leaks or unintended copies.
 */
class Tuile {
public:
    /**
     * @brief Construct a Tuile with an identifier.
     * @param id Unique identifier or name of the tile (e.g. "tile_0_0" or file name).
     */
    explicit Tuile(std::string id = "");

    /**
     * @brief Construct a Tuile with an identifier and an owned Pyramide.
     * @param id Unique identifier.
     * @param pyramide Unique pointer to an associated Pyramide.
     */
    Tuile(std::string id, std::unique_ptr<Pyramide> pyramide);

    // Virtual destructor for safety in polymorphic pipelines
    virtual ~Tuile();

    // Prevent copying to avoid duplicating ownership and dangling neighbor pointers
    Tuile(const Tuile&) = delete;
    Tuile& operator=(const Tuile&) = delete;

    // Move operations
    Tuile(Tuile&& other) noexcept;
    Tuile& operator=(Tuile&& other) noexcept;

    // --- Identifier ---

    const std::string& getId() const noexcept { return id_; }
    void setId(std::string id) { id_ = std::move(id); }

    // --- UML Neighbor Getters ---

    Tuile* getLeft() noexcept { return left_; }
    const Tuile* getLeft() const noexcept { return left_; }

    Tuile* getRight() noexcept { return right_; }
    const Tuile* getRight() const noexcept { return right_; }

    Tuile* getTop() noexcept { return top_; }
    const Tuile* getTop() const noexcept { return top_; }

    Tuile* getBottom() noexcept { return bottom_; }
    const Tuile* getBottom() const noexcept { return bottom_; }

    // --- Neighbor Setters ---

    void setLeft(Tuile* neighbor) noexcept { left_ = neighbor; }
    void setRight(Tuile* neighbor) noexcept { right_ = neighbor; }
    void setTop(Tuile* neighbor) noexcept { top_ = neighbor; }
    void setBottom(Tuile* neighbor) noexcept { bottom_ = neighbor; }

    // --- Bidirectional Connection Helpers ---

    /**
     * @brief Establish a reciprocal horizontal neighbor relationship:
     *        this->right = rightNeighbor, rightNeighbor->left = this.
     */
    void connectRight(Tuile* rightNeighbor) noexcept;

    /**
     * @brief Establish a reciprocal vertical neighbor relationship:
     *        this->bottom = bottomNeighbor, bottomNeighbor->top = this.
     */
    void connectBottom(Tuile* bottomNeighbor) noexcept;

    /**
     * @brief Disconnect this tile from all adjacent neighbors,
     *        safely clearing reciprocal pointers on neighbors to prevent dangling references.
     */
    void disconnectAll() noexcept;

    // --- Pyramide Association & Ownership ---

    bool hasPyramide() const noexcept { return pyramide_ != nullptr; }

    const Pyramide* getPyramide() const noexcept { return pyramide_.get(); }
    Pyramide* getPyramide() noexcept { return pyramide_.get(); }

    void setPyramide(std::unique_ptr<Pyramide> pyramide) noexcept {
        pyramide_ = std::move(pyramide);
    }

    std::unique_ptr<Pyramide> releasePyramide() noexcept {
        return std::move(pyramide_);
    }

private:
    std::string id_;

    // Exclusive ownership of the multiresolution pyramid metadata
    std::unique_ptr<Pyramide> pyramide_{nullptr};

    // Non-owning pointers to adjacent neighbors in the mosaic grid.
    // nullptr indicates boundary of mosaic (missing neighbor).
    Tuile* left_{nullptr};
    Tuile* right_{nullptr};
    Tuile* top_{nullptr};
    Tuile* bottom_{nullptr};
};
