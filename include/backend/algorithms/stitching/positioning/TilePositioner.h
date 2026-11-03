// include/backend/algorithms/stitching/positioning/TilePositioner.h
#pragma once
#include "algorithms/legacy_type_aliases.h"
#include <iosfwd>
#include <string>
#include <vector>

namespace stitching::positioning {

struct PositioningSettings {
    float overlap = 0.3f;                    // fraction maximale de la tuile examinée par SIFT (bande de recouvrement)
    std::string matcher = "sift";            // noms de la Factory
    // [AD 21/10/2026 20:41] Rejet des aberrants par défaut : "vote" (Hough 2D déterministe O(N), sans fausse rotation sur bande étroite).
    // Reste 100% compatible avec "ransac" (estimateAffinePartial2D) si spécifié dans settings.
    std::string rejector = "vote";
    Json matcherParams = Json::object();
    Json rejectorParams = Json::object();
};

// Calcule la position (coin haut-gauche) de chaque tuile à partir des seules images :
//   pour chaque paire de voisins directs (droite, dessous ; pas de diagonale) :
//     SIFT (lecture 16 bits sans perte) -> RANSAC -> translation = médiane de (pointA - pointB) sur les INLIERS
//   puis ajustement global par moindres carrés de toutes les translations.
//
// tile_paths : tuiles nommées "..._rLL_cCC.tif(f)". La première tuile (plus petite ligne, puis colonne) sert de
//              référence en (0, 0). Une paire dont SIFT ou RANSAC échoue est ignorée (la grille reste connectée par
//              les autres paires si possible).
// positions  : une entrée par tuile reliée à la référence ; identifiant "rLL_cCC" (le même que ImagePretreatment).
//              Si positions.size() != tile_paths.size(), certaines tuiles ne sont pas reliées : voir le journal.
// log        : messages de progression et diagnostics (résidus par paire).
// Retourne false en cas d'erreur (nom invalide, doublon, lecture impossible, aucune paire exploitable).
bool computeTilePositions(const std::vector<std::string>& tile_paths,
                          const PositioningSettings& settings,
                          std::vector<TilePositionData>& positions,
                          std::ostream& log);

} // namespace stitching::positioning
