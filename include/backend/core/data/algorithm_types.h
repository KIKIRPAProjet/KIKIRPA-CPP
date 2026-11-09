// include/backend/core/data/algorithm_types.h
#pragma once

#include <opencv2/core.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace DataNs {

    // Position de la tuile B par rapport à la tuile A. PairMatches n'utilise que Right (B à droite de A) et Bottom (B sous A).
    enum class Direction {
        Top,
        Bottom,
        Left,
        Right,
    };

    // Image d'une tuile chargée en mémoire ; gridColumn/gridRow = position dans la grille, lue dans le nom du fichier.
    struct TileImage {
        std::filesystem::path filePath;
        int gridColumn = 0;
        int gridRow = 0;
        cv::Mat pixels;
    };
    using TileImages = std::vector<TileImage>;

    // Contexte d'une paire de tuiles voisines, transmis aux algorithmes d'appariement.
    struct PairContext {
        Direction direction = Direction::Right;
        float overlapFraction = 0.f;   // recouvrement en fraction de la tuile : nominal, ou réel une fois estimé
        // Coin haut-gauche de B dans le repère de A, au pixel près, mesuré par la première passe (OverlapEstimationNs::OverlapEstimatorClass).
        // Absent : l'algorithme part du recouvrement nominal overlapFraction.
        std::optional<cv::Point2d> estimatedTranslation;
    };

    // Sortie de la première passe (OverlapEstimationNs::OverlapEstimatorClass), commune à toutes les méthodes d'appariement.
    struct OverlapEstimate {
        cv::Point2d translationBInA;   // coin haut-gauche de B dans le repère de A, au pixel près
        float overlapFraction = 0.f;   // recouvrement réel, en fraction de la tuile dans le sens du déplacement
        double peakRatio = 0.0;        // rapport pic / second pic de la mesure : confiance
    };

    // Sortie d'un StitchingNs::FeatureMatcherClass : pointsInA[i] correspond à pointsInB[i].
    struct Matches {
        std::vector<cv::Point2f> pointsInA;
        std::vector<cv::Point2f> pointsInB;
        std::vector<float> confidenceScores;   // un score entre 0 et 1 par paire de points
        float translationX = 0.f;              // translation estimée de B dans le repère de A, en pixels
        float translationY = 0.f;              // (pointsInA - pointsInB)
    };

    // Appariement d'une paire de tuiles voisines (type "PairMatches", clé "matches"),
    // produit par les steps d'appariement (corrélation de phase, SIFT, LightGlue...).
    struct PairMatches {
        int tileIdA = 0;   // TileClass::getId() de A et de B
        int tileIdB = 0;
        Direction direction = Direction::Right;   // position de B par rapport à A
        Matches featureMatches;
    };

    // Sortie d'un RejectionNs::OutlierRejectorClass.
    struct Alignment {
        cv::Mat transformBToA;           // transformation de B vers A (3x3, CV_64F)
        std::vector<uchar> inlierMask;   // 1 = inlier ; même taille que les points de Matches
    };

    // Position finale d'une tuile, calculée par le placement global (PositioningNs::computeTilePositions).
    struct TilePositionData {
        std::string tileId;     // nom de la tuile, de la forme "rLL_cCC" (ligne, colonne)
        cv::Point2d position;   // coin haut-gauche dans le repère absolu de la mosaïque
        double rotationRadians = 0.0;   // rotation absolue de la tuile, en radians
    };

    // Territoire d'une tuile : polygones, dans le repère de la mosaïque, délimitant les pixels pris dans cette tuile.
    // polygons[i] = contour extérieur ; holes[i] = trous du polygone i.
    struct TileTerritory {
        int tileIndex = 0;   // indice de la tuile dans la liste des tuiles
        std::vector<std::vector<cv::Point>> polygons;
        std::vector<std::vector<std::vector<cv::Point>>> holes;
    };

    constexpr int BLOCK_SIZE = 512;

    // Bloc calculé par l'assemblage / fusion ; les matrices ont la taille de blockRect.
    struct CompositeBlock {
        cv::Mat imageRgb;       // CV_16UC3, canaux RGB ou BGR
        cv::Mat provenance;     // CV_16UC1, identifiant de tuile
        cv::Mat mask;           // CV_8UC1, 255 = pixel valide
        cv::Rect blockRect;     // rectangle du bloc sur la mosaïque, en pixels
    };

    using CompositeBlockList = std::vector<CompositeBlock>;

}
