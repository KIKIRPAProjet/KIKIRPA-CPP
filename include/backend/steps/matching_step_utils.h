// include/backend/steps/matching_step_utils.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

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

    // Lit les pixels sans modifier la profondeur. Lève une exception si le fichier est illisible
    // ou si son format n'est pas une image 2D non signée 8/16 bits à 1, 3 ou 4 canaux.
    cv::Mat readImage(const DataNs::TileClass& tile);

    // Vérifie les tailles, bornes, scores et translations ; lève std::runtime_error si invalides.
    void validateMatches(const DataNs::Matches& matches, cv::Size imageSizeA,
                         cv::Size imageSizeB, int minimumMatchCount);

    // Ajoute les origines des bandes aux points et leur différence à la translation (pixels).
    void restoreCoordinates(DataNs::Matches& matches, const cv::Rect& regionA, const cv::Rect& regionB);

    // Enveloppe JSON key/value/type ; lève std::invalid_argument pour une direction non prise en charge.
    nlohmann::json serializeOutput(const std::string& key, const std::vector<DataNs::PairMatches>& pairs,
                                  const std::string& type);

}
