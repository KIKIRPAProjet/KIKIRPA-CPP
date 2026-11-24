// include/frontend/image_layer_loader.h
#pragma once

#include "core/data/tile.h"
#include "image_layer.h"

#include <QVector>

#include <vector>

namespace DataNs {
    struct TilePositionData;
}

// Charge un niveau TIFF. La position, en pixels pleine résolution, doit avoir le même identifiant que la tuile.
// Renvoie une couche vide avec un avertissement si la rotation est non nulle.
// Lève une exception si le niveau, l'association, la lecture ou la conversion est invalide.
ImageLayer loadLowResolutionLayer(
    const DataNs::TileClass& tile,
    const DataNs::TilePositionData& position,
    int level = 3);

// Associe les tuiles et positions par identifiant, puis charge les couches dans l'ordre des tuiles.
// Lève une exception si les identifiants ne correspondent pas un à un ou si une lecture échoue.
// Les tuiles en rotation sont omises du rendu mais comptent dans les limites de la mosaïque.
QVector<ImageLayer> buildImageLayers(
    const DataNs::TileList& tiles,
    const std::vector<DataNs::TilePositionData>& positions,
    int level = 3,
    QRectF* bounds = nullptr);
