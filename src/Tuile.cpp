#include "Tuile.hpp"

Tuile::Tuile(std::string id)
    : id_(std::move(id)),
      pyramide_(nullptr),
      left_(nullptr),
      right_(nullptr),
      top_(nullptr),
      bottom_(nullptr) {}

Tuile::Tuile(std::string id, std::unique_ptr<Pyramide> pyramide)
    : id_(std::move(id)),
      pyramide_(std::move(pyramide)),
      left_(nullptr),
      right_(nullptr),
      top_(nullptr),
      bottom_(nullptr) {}

Tuile::~Tuile() {
    // Automatically disconnect reciprocal pointers on neighbors to prevent dangling pointers.
    disconnectAll();
}

Tuile::Tuile(Tuile&& other) noexcept
    : id_(std::move(other.id_)),
      pyramide_(std::move(other.pyramide_)),
      left_(other.left_),
      right_(other.right_),
      top_(other.top_),
      bottom_(other.bottom_) {
    // Update neighbors' reciprocal pointers to point to this new object instance
    if (left_ && left_->right_ == &other) left_->right_ = this;
    if (right_ && right_->left_ == &other) right_->left_ = this;
    if (top_ && top_->bottom_ == &other) top_->bottom_ = this;
    if (bottom_ && bottom_->top_ == &other) bottom_->top_ = this;

    // Reset other's neighbor pointers
    other.left_ = nullptr;
    other.right_ = nullptr;
    other.top_ = nullptr;
    other.bottom_ = nullptr;
}

Tuile& Tuile::operator=(Tuile&& other) noexcept {
    if (this != &other) {
        // Disconnect old neighbors of this tile first
        disconnectAll();

        id_ = std::move(other.id_);
        pyramide_ = std::move(other.pyramide_);
        left_ = other.left_;
        right_ = other.right_;
        top_ = other.top_;
        bottom_ = other.bottom_;

        // Update neighbors' reciprocal pointers
        if (left_ && left_->right_ == &other) left_->right_ = this;
        if (right_ && right_->left_ == &other) right_->left_ = this;
        if (top_ && top_->bottom_ == &other) top_->bottom_ = this;
        if (bottom_ && bottom_->top_ == &other) bottom_->top_ = this;

        // Reset other's pointers
        other.left_ = nullptr;
        other.right_ = nullptr;
        other.top_ = nullptr;
        other.bottom_ = nullptr;
    }
    return *this;
}

void Tuile::connectRight(Tuile* rightNeighbor) noexcept {
    // If we already had a right neighbor, detach it
    if (right_ && right_->left_ == this) {
        right_->left_ = nullptr;
    }

    right_ = rightNeighbor;
    if (rightNeighbor != nullptr) {
        // If the right neighbor already had a left neighbor, detach it
        if (rightNeighbor->left_ && rightNeighbor->left_ != this) {
            rightNeighbor->left_->right_ = nullptr;
        }
        rightNeighbor->left_ = this;
    }
}

void Tuile::connectBottom(Tuile* bottomNeighbor) noexcept {
    // If we already had a bottom neighbor, detach it
    if (bottom_ && bottom_->top_ == this) {
        bottom_->top_ = nullptr;
    }

    bottom_ = bottomNeighbor;
    if (bottomNeighbor != nullptr) {
        // If the bottom neighbor already had a top neighbor, detach it
        if (bottomNeighbor->top_ && bottomNeighbor->top_ != this) {
            bottomNeighbor->top_->bottom_ = nullptr;
        }
        bottomNeighbor->top_ = this;
    }
}

void Tuile::disconnectAll() noexcept {
    if (left_ && left_->right_ == this) {
        left_->right_ = nullptr;
    }
    if (right_ && right_->left_ == this) {
        right_->left_ = nullptr;
    }
    if (top_ && top_->bottom_ == this) {
        top_->bottom_ = nullptr;
    }
    if (bottom_ && bottom_->top_ == this) {
        bottom_->top_ = nullptr;
    }

    left_ = nullptr;
    right_ = nullptr;
    top_ = nullptr;
    bottom_ = nullptr;
}
