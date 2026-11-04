// include/backend/algorithms/stitching/positioning/tile_positioner.h
#pragma once

#include "core/data/algorithm_types.h"

#include <nlohmann/json.hpp>

#include <iosfwd>
#include <string>
#include <vector>

namespace PositioningNs {

    using Json = nlohmann::json;

    struct PositioningSettings {
        float overlapFraction = 0.3f;   // fraction maximale de la tuile examinée par SIFT (bande de recouvrement)
        // Méthode d'appariement : "sift", "correlation_phase" (corrélation de phase) ou "lightglue" (si LightGlue est
        // compilé)
        std::string matcherName = "sift";
        // Rejet des aberrants : "vote" (Vote de Hough 2D déterministe O(N), sans fausse rotation sur bande étroite).

        std::string rejectorName = "vote";
        Json matcherParameters = Json::object();
        Json rejectorParameters = Json::object();
    };

    // Calcule la position (coin haut-gauche) de chaque tuile à partir des seules images :
    //   pour chaque paire de voisins directs (droite, dessous ; pas de diagonale) :
    //     SIFT (lecture 16 bits sans perte) -> Vote 2D -> translation = médiane de (pointInA - pointInB) sur les INLIERS
    //   puis ajustement global par moindres carrés de toutes les translations.
    //
    // tilePaths : tuiles nommées "..._rLL_cCC.tif(f)". La première tuile (plus petite ligne, puis colonne) sert de
    //             référence en (0, 0). Une paire dont SIFT ou le Vote 2D échoue est ignorée (la grille reste connectée par
    //             les autres paires si possible).
    // positions : une entrée par tuile reliée à la référence ; identifiant "rLL_cCC" (le même que
    //             ImagePretreatmentClass). Si positions.size() != tilePaths.size(), certaines tuiles ne sont pas reliées :
    //             voir le journal.
    // log       : messages de progression et diagnostics (résidus par paire).
    // Retourne false en cas d'erreur (nom invalide, doublon, lecture impossible, aucune paire exploitable).
    bool computeTilePositions(const std::vector<std::string>& tilePaths, const PositioningSettings& settings,
                              std::vector<DataNs::TilePositionData>& positions, std::ostream& log);

}
