// include/backend/core/data/algorithm_types.h
#pragma once

#include <opencv2/core.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace DataNs {

    // Position de la tuile B par rapport à la tuile A. PairMatches n'utilise que Right (B à droite de A) et Bottom (B sous A).
    enum class Direction { Top, Bottom, Left, Right };

    // Image d'une tuile chargée en mémoire ; gridColumn/gridRow = position dans la grille, lue dans le nom du fichier.
    struct TileImage {
        std::filesystem::path filePath;
        int gridColumn = 0;
        int gridRow = 0;
        cv::Mat pixels;
    };
    using TileImages = std::vector<TileImage>;

    struct PairContext {
        Direction direction = Direction::Right;
        float overlapFraction = 0.f;
        std::optional<cv::Point2d> estimatedTranslation;
    };

    struct OverlapEstimate {
        cv::Point2d translationBInA;
        float overlapFraction = 0.f;
        double peakRatio = 0.0;
    };

    // pointsInA[i] correspond à pointsInB[i] ; la translation est le coin haut-gauche de B dans le repère de A.
    struct Matches {
        std::vector<cv::Point2f> pointsInA;
        std::vector<cv::Point2f> pointsInB;
        std::vector<float> confidenceScores;
        float translationX = 0.f;
        float translationY = 0.f;
    };

    struct PairMatches {
        int tileIdA = 0;
        int tileIdB = 0;
        Direction direction = Direction::Right;
        Matches featureMatches;
    };

    struct Alignment {
        cv::Mat transformBToA;           // matrice 3x3 CV_64F, transforme les coordonnées de B vers A
        std::vector<uchar> inlierMask;
    };

    struct TilePositionData {
        std::string tileId;              // identifiant rLL_cCC, ligne puis colonne
        cv::Point2d position;            // coin haut-gauche, coordonnées pixel (x=colonne, y=ligne) du canevas absolu
        double rotationRadians = 0.0;
    };

    // Corrections estimées pour une tuile. Le gain et le champ s'appliquent aux canaux couleur avant assemblage.
    struct TileCorrection {
        int tileId = 0;
        double gain = 1.0;
        double fieldStrength = 0.0;
    };
    using TileCorrections = std::vector<TileCorrection>;

    // Territoire d'une tuile dans le repère du canevas de mosaïque normalisé (origine au minimum des positions).
    struct TileTerritory {
        int tileIndex = 0;
        std::vector<std::vector<cv::Point>> polygons;
        std::vector<std::vector<std::vector<cv::Point>>> holes;
    };

    // Mosaïque raster pleine résolution. Les pixels restent dans le type natif produit par le compositeur.
    struct MosaicData {
        cv::Mat pixels;
    };

}
