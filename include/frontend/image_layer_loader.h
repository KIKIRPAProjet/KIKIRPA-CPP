
#pragma once

#include "image_layer.h"

#include "../backend/core/data/tile.h"
#include "../backend/core/data/algorithm_types.h"
#include <QVector>
#include <vector>
// Charge une tuile à basse résolution et crée sa couche Qt.
// La position doit correspondre à la tuile fournie.
ImageLayer loadLowResolutionLayer(
    const DataNs::TileClass &tile,
    const DataNs::TilePositionData &position,
    int level = 3
);
QVector<ImageLayer> buildImageLayers(
    const DataNs::TileList& tiles,
    const std::vector<DataNs::TilePositionData>& positions,
    int level = 3
);
