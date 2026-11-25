// src/frontend/image_layer_loader.cpp
#include "image_layer_loader.h"

#include "core/data/algorithm_types.h"
#include "core/io/tile_pixel_reader.h"
#include "core/io/unicode_path.h"
#include "frontendutils.h"

#include <QDebug>
#include <opencv2/core/mat.hpp>

#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
QRectF layerWorldRect(const DataNs::TileClass& tile, const DataNs::TilePositionData& position, int level)
{
    const double scale = std::ldexp(1.0, level);
    return {position.position.x / scale, position.position.y / scale,
            tile.getLevel(0).width / scale, tile.getLevel(0).height / scale};
}
}

ImageLayer loadLowResolutionLayer(
    const DataNs::TileClass& tile,
    const DataNs::TilePositionData& position,
    int level)
{
    const int tileId = tile.getId();
    const std::string context = "Tile ID " + std::to_string(tileId) +
                                ", TIFF level " + std::to_string(level);
    if (level < 0 || level >= DataNs::LEVEL_COUNT) {
        throw std::invalid_argument(context + ": invalid pyramid level");
    }
    if (position.tileId != tileId) {
        throw std::invalid_argument(context + ": position belongs to tile ID " +
                                    std::to_string(position.tileId));
    }

    // Le rectangle de la couche ne représente qu'une translation.
    if (position.rotationRadians != 0.0) {
        qWarning().noquote() << "Skipping image layer for tile ID" << tileId
                             << "because its rotation is unsupported (radians):"
                             << position.rotationRadians;
        return {};
    }

    // Par TilePixelReaderClass et non par le cache directement : depuis le 24/11/2026 le cache ne contient
    // plus le niveau 0 quand l'image de l'œuvre sait servir une région. Lire le cache à la main rendrait
    // alors le niveau 1 en croyant tenir la pleine résolution, soit un calque à la moitié de son échelle,
    // sans aucune erreur. Cette classe est la seule à savoir où vit chaque niveau.
    const IONs::TilePixelReaderClass reader(tile.getSource(), tile.getTiff());
    cv::Mat pixels;
    std::string errorMessage;
    if (!reader.readLevel(level, pixels, errorMessage)) {
        throw std::runtime_error(context + ": cannot read '" +
                                 IONs::pathToUtf8(tile.getTiff()) + "': " + errorMessage);
    }

    const QImage preview = FrontendUtils::matToQImage(pixels);
    if (preview.isNull()) {
        throw std::runtime_error(context + ": cannot convert TIFF pixels to a preview");
    }

    // La position absolue est exprimée en pixels de pleine résolution.
    ImageLayer layer;
    layer.id = QString::number(tileId);
    layer.name = QString::fromStdString(IONs::pathToUtf8(tile.getSource().filename()));
    layer.image = preview;
    layer.worldRect = layerWorldRect(tile, position, level);
    return layer;
}

QVector<ImageLayer> buildImageLayers(
    const DataNs::TileList& tiles,
    const std::vector<DataNs::TilePositionData>& positions,
    int level,
    QRectF* bounds)
{
    std::map<int, const DataNs::TilePositionData*> positionsById;
    for (const auto& position : positions) {
        if (!positionsById.emplace(position.tileId, &position).second) {
            throw std::invalid_argument("Duplicate position for tile ID " +
                                        std::to_string(position.tileId));
        }
    }

    std::set<int> tileIds;
    for (const auto& tile : tiles) {
        if (!tile) {
            throw std::invalid_argument("Tile list contains a null tile");
        }
        const int tileId = tile->getId();
        if (!tileIds.insert(tileId).second) {
            throw std::invalid_argument("Duplicate tile ID " + std::to_string(tileId));
        }
        if (positionsById.count(tileId) == 0) {
            throw std::invalid_argument("Missing position for tile ID " + std::to_string(tileId));
        }
    }

    for (const auto& position : positions) {
        if (tileIds.count(position.tileId) == 0) {
            throw std::invalid_argument("Position has no matching tile for ID " +
                                        std::to_string(position.tileId));
        }
    }

    QVector<ImageLayer> layers;
    QRectF worldBounds;
    for (const auto& tile : tiles) {
        const auto& position = *positionsById.at(tile->getId());
        ImageLayer layer = loadLowResolutionLayer(*tile, position, level);
        if (bounds) worldBounds = worldBounds.united(layerWorldRect(*tile, position, level));
        if (!layer.image.isNull()) {
            layers.push_back(std::move(layer));
        }
    }
    if (bounds) *bounds = worldBounds;
    return layers;
}
