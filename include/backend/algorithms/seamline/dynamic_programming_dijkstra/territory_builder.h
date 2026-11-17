// include/backend/algorithms/seamline/dynamic_programming_dijkstra/territory_builder.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"

#include <string>
#include <vector>

namespace DynamicProgrammingDijkstraNs {

    struct TerritoryInput {
        const DataNs::TileList& tiles;
        const std::vector<DataNs::TilePositionData>& positions;
        const DataNs::Seamlines& seamlines;
    };
    // Associe les positions à TileClass::getId() et les range dans l'ordre de tiles.
    // Refuse les positions manquantes ou dupliquées. En cas d'échec, positions
    // reste inchangé et error contient la cause ; sinon error est vide.
    bool orderTilePositions(const DataNs::TileList& tiles,
        std::vector<DataNs::TilePositionData>& positions,
        std::string& error);

    // Construit les territoires dans le repère entier utilisé par FusionStep.
    // Les coutures d'entrée restent inchangées. Les pixels sur une couture
    // appartiennent à A ; sans couture, la première tuile reste prioritaire.
    // Les polygones sont fragmentés par blocs pour limiter la mémoire de travail.
    // Renvoie false avec la cause dans error si les entrées sont incohérentes.
    bool buildTerritories(const TerritoryInput& input,
        std::vector<DataNs::TileTerritory>& territories,
        std::string& error);

}