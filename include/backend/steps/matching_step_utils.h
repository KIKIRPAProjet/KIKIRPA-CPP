// include/backend/steps/matching_step_utils.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"

#include <nlohmann/json_fwd.hpp>

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <optional>
#include <string>
#include <vector>

namespace StepsNs {

    struct NeighborPair {
        const DataNs::TileClass* tileA;
        const DataNs::TileClass* tileB;
        DataNs::Direction direction;
    };

    // Collecte chaque voisine droite/bas une fois. Lève std::invalid_argument si le graphe
    // contient des tuiles nulles, des identifiants dupliqués ou des voisinages incohérents.
    std::vector<NeighborPair> collectPairs(const DataNs::TileList& tiles);

    // Lit la pleine résolution sans modifier la profondeur, **en RGB** (core/io/tile_pixel_reader.h : par le
    // cache quand celui-ci contient le niveau 0, sinon par l'image de l'œuvre — un cache réduit n'a pas la pleine
    // résolution). Lève une exception si le fichier est illisible ou si son format n'est pas
    // une image 2D non signée 8/16 bits à 1, 3 ou 4 canaux.
    cv::Mat readImage(const DataNs::TileClass& tile);

    // Vérifie les tailles, bornes, scores et translations ; lève std::runtime_error si invalides.
    void validateMatches(const DataNs::Matches& matches, cv::Size imageSizeA,
                         cv::Size imageSizeB, int minimumMatchCount);

    // Ajoute les origines des bandes aux points et leur différence à la translation (pixels).
    void restoreCoordinates(DataNs::Matches& matches, const cv::Rect& regionA, const cv::Rect& regionB);

    // Mesures de la première passe publiées par le step overlap-estimation (clé "overlapEstimates"), ou nullptr si le
    // pipeline n'en contient pas. Les steps d'appariement s'en servent quand elles existent, sans l'exiger.
    const DataNs::OverlapEstimates* findOverlapEstimates(const StepNs::DataStoreClass& dataStore);

    // Translation de B dans A (pixels pleine résolution) mesurée par overlap-estimation pour la paire (tileIdA, tileIdB).
    // Absente si `estimates` est nul ou si la paire n'a pas été mesurée : le step fait alors sa propre première passe.
    std::optional<cv::Point2d> findEstimatedTranslation(const DataNs::OverlapEstimates* estimates, int tileIdA, int tileIdB);

    // Recouvrement nominal d'une paire : verticalOverlap pour une voisine du dessus ou du dessous s'il est > 0, overlap
    // sinon (voisine de droite ou de gauche, ou verticalOverlap = 0 : « comme overlap »).
    float overlapForDirection(DataNs::Direction direction, float overlap, float verticalOverlap);

    // Contrôle les paramètres overlap (dans ]0, 1[) et verticalOverlap (0, ou dans ]0, 1[). Message d'erreur, vide si
    // les deux sont valides.
    std::string checkOverlaps(float overlap, float verticalOverlap);

    // Recouvrement réel d'une paire, tel que l'a vu la première passe : mesuré (paire réussie) ou indiqué par un pic
    // franc refusé parce qu'hors de la marge du rail (PhaseCorrelationNs::OutOfMarginPeak).
    struct OverlapObservation {
        DataNs::Direction direction;   // Right ou Bottom (position de B par rapport à A)
        double overlapFraction;        // fraction de la tuile dans le sens du déplacement
        bool isOutOfMargin;            // true : pic hors marge (paire refusée) ; false : paire mesurée
    };

    // Recouvrement réel de la paire mesurée : la translation (coin de B dans A) dans une tuile de taille tileSize.
    double measuredOverlapFraction(DataNs::Direction direction, cv::Size tileSize, cv::Point2d translation);

    // Recalage du recouvrement nominal d'une direction (Right : paires horizontales ; Bottom : verticales). Le rail fait
    // le même pas pour toutes les paires d'une direction : si plusieurs pics francs hors marge s'accordent avec les
    // paires mesurées, le recouvrement nominal annoncé est faux, et la médiane des observations le remplace. Renvoie
    // cette médiane si au moins une paire de la direction a un pic hors marge, si au moins
    // MINIMUM_AGREEING_OBSERVATIONS observations sont à moins de OVERLAP_AGREEMENT_TOLERANCE de la médiane, et si elles
    // sont la majorité. Vide sinon : un seul pic hors marge (motif répété possible) n'est jamais cru.
    inline constexpr int MINIMUM_AGREEING_OBSERVATIONS = 2;
    inline constexpr double OVERLAP_AGREEMENT_TOLERANCE = 0.02;   // fraction de la tuile (2 %)
    std::optional<double> calibrateOverlap(const std::vector<OverlapObservation>& observations, DataNs::Direction direction);

    // Enveloppe JSON key/value/type ; lève std::invalid_argument pour une direction non prise en charge.
    nlohmann::json serializeOutput(const std::string& key, const std::vector<DataNs::PairMatches>& pairs,
                                  const std::string& type);

}
