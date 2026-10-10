#pragma once
#include <opencv2/core.hpp>
#include <memory>
#include <vector>

namespace stitching {

using Tile = cv::Mat;                       // Tile : Mat (OpenCV)
using Grid = std::vector<std::vector<Tile>>; // [][]Tile

class Pyramide;

class Niveau {
public:
    Grid data;
    Pyramide* parent = nullptr;
};

class Tuile;

class Pyramide {
public:
    Grid original;
    Niveau half, quarter, eighth;
    Tuile* parent = nullptr;   // pointeur, sinon inclusion circulaire
    explicit Pyramide(Tuile* p = nullptr);
};

class Tuile {
    Tuile *left = nullptr, *right = nullptr, *top = nullptr, *bottom = nullptr;
    std::unique_ptr<Pyramide> pyramide;      // Tuile --> Pyramide (ownership)
public:
    Tuile() : pyramide(std::make_unique<Pyramide>(this)) {}
    Tuile* getLeft()   const { return left; }
    Tuile* getRight()  const { return right; }
    Tuile* getTop()    const { return top; }
    Tuile* getBottom() const { return bottom; }
    Pyramide& getPyramide() { return *pyramide; }

    // Image passée aux algorithmes (vide si rien n'est chargé)
    // TODO : choisir le bon niveau de pyramide / la bonne sous-tuile
    cv::Mat image() const;
};

} // namespace stitching
