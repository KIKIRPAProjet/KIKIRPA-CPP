// include/Backend/core/data/stitching_types.h
#pragma once

#include <opencv2/core.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace DataNs {

    // Position de la tuile B par rapport à la tuile A.
    // Top/Bottom/Left/Right : corrélation de phase. LeftRight et BottomRight (anciens noms, SIFT et placement
    // global) valent respectivement Right (B à droite de A) et Bottom (B sous A).
    enum class Direction {
        Top,
        Bottom,
        Left,
        Right,
    };

    // Images chargées en mémoire par un traitement ponctuel.
    struct TileImage {
        std::filesystem::path filePath;
        int gridColumn = 0;
        int gridRow = 0;   // position dans la grille, lue dans le nom du fichier
        cv::Mat pixels;
    };
    // Il y avait une erreur de définition (using Tiles = ... dans Tuile.h et ici) -> pense que ces deux définitions sont différentes, je les ai séparées
    using TileImages = std::vector<TileImage>;

    // Contexte d'une paire de tuiles voisines, transmis aux algorithmes
    struct PairContext {
        Direction direction = Direction::Right;
        float overlapFraction = 0.f;   // recouvrement (fraction de la tuile) : nominal, ou réel une fois estimé
        // Coin haut-gauche de B dans le repère de A, au pixel près, mesuré par la première passe
        // (EstimationRecouvrement). Absent : l'algorithme part du recouvrement nominal `overlap`.
        std::optional<cv::Point2d> estimatedTranslation;
    };

    // Sortie de la première passe (EstimationRecouvrement), commune à toutes les méthodes d'appariement
    struct OverlapEstimate {
        cv::Point2d translationBInA;   // coin haut-gauche de B dans le repère de A, au pixel près
        float overlapFraction = 0.f;       // recouvrement réel : fraction de la tuile dans le sens du déplacement
        double peakRatio = 0.0;      // rapport pic / second pic de la mesure (confiance)
    };

    // Sortie d'un FeatureMatcher : pointsA[i] <-> pointsB[i]
    //   dx, dy : translation estimée (coin haut-gauche de B dans le repère de A, en px : pointsA - pointsB)
    //   scores : score de confiance de chaque paire de points (entre 0 et 1)
    struct Matches {
        std::vector<cv::Point2f> pointsInA;
        std::vector<cv::Point2f> pointsInB;
        std::vector<float> confidenceScores;
        float translationX = 0.f;
        float translationY = 0.f;
    };

    // PipelineOption "PairMatches" (clé "matches") : appariement d'une paire de tuiles voisines,
    // produit par les steps d'appariement (corrélation de phase, SIFT, LightGlue...)
    struct PairMatches {
        int tileIdA = 0;
        int tileIdB = 0;                 // Tuile::getId() de A et de B
        Direction direction = Direction::Right;     // position de B par rapport à A (Right ou Bottom)
        Matches featureMatches;
    };

    // Sortie d'un OutlierRejector
    struct Alignment {
        cv::Mat transformBToA;            // transformation b -> a (3x3, CV_64F)
        std::vector<uchar> inlierMask;   // 1 = inlier, même taille que Matches
    };

    // Position d'une tuile calculée par le placement global (TilePositioner)
    struct TilePositionData {
        std::string tileId;
        cv::Point2d position;
        // [AD 21/10/2026 20:41] Angle de rotation absolu de la tuile (en radians) résolu globalement par TilePositioner.
        // La valeur par défaut 0.0 garantit une rétrocompatibilité totale avec les appels existants {id, position}.
        double rotationRadians = 0.0;
    };

    // Territoire d'une tuile : polygones (repère de la mosaïque) qui délimitent les pixels pris dans cette tuile.
    // polygons[i] = contour extérieur ; holes[i] = trous du polygone i.
    struct TileTerritory {
        int tileIndex = 0;
        std::vector<std::vector<cv::Point>> polygons;
        std::vector<std::vector<std::vector<cv::Point>>> holes;
    };

}
