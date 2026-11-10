
#include "image_layer_loader.h"

#include "backend/core/io/pyramidal_tiff_reader.h"
#include <opencv2/core.hpp>

#include <cmath>
#include <stdexcept>
#include <string>
#include "algorithms/seamline/graph_cut/image_pretreatment.h"

#include <map>
#include <set>
ImageLayer loadLowResolutionLayer(
    const DataNs::TileClass &tile,
    const DataNs::TilePositionData &position,
    int level)
{
    // 1. Vérifier le niveau demandé.
    if (level < 0 || level >= DataNs::LEVEL_COUNT) {
        throw std::invalid_argument("Niveau TIFF invalide");
    }

    // Première version : rotations non encore prises en charge.
    if (std::abs(position.rotationRadians) > 1e-9) {
        throw std::runtime_error(
            "La rotation des couches n'est pas encore geree"
        );
    }

    // 2. Obtenir les dimensions de l'image réduite.
    const DataNs::Level &info = tile.getLevel(level);

    if (info.width <= 0 || info.height <= 0) {
        throw std::runtime_error("Dimensions TIFF invalides");
    }

    // 3. Lire les pixels du niveau demandé.
    IONs::PyramidalTiffReaderClass reader(tile.getTiff());

    cv::Mat pixels;
    std::string errorMessage;

    const cv::Rect region(
        0, 0,
        info.width,
        info.height
    );

    if (!reader.readRegion(level, region, pixels, errorMessage)) {
        throw std::runtime_error(
            "Echec lecture TIFF : " + errorMessage
        );
    }

    // 4. Convertir cv::Mat vers QImage.
    QImage preview;

    if (pixels.type() == CV_8UC1) {
        preview = QImage(
            pixels.data,
            pixels.cols,
            pixels.rows,
            static_cast<qsizetype>(pixels.step),
            QImage::Format_Grayscale8
        ).copy();
    }
    else if (pixels.type() == CV_16UC1) {
        preview = QImage(
            pixels.data,
            pixels.cols,
            pixels.rows,
            static_cast<qsizetype>(pixels.step),
            QImage::Format_Grayscale16
        ).copy();
    }
    else {
        throw std::runtime_error(
            "Format de pixels non pris en charge"
        );
    }

    // 5. Convertir les positions pleine résolution
    // vers les coordonnées du niveau TIFF.
    const double scale = std::ldexp(1.0, level);

    // 6. Construire la couche.
    ImageLayer layer;

    layer.id = QString::fromStdString(position.tileId);
    layer.name = layer.id;
    layer.image = preview;

    layer.worldRect = QRectF(
        position.position.x / scale,
        position.position.y / scale,
        info.width,
        info.height
    );

    layer.opacity = 1.0;
    layer.visible = true;

    return layer;
}
QVector<ImageLayer> buildImageLayers(
    const DataNs::TileList &tiles,
    const std::vector<DataNs::TilePositionData> &positions,
    int level)
{
    // 1. Associer chaque identifiant à sa position.
    std::map<std::string, const DataNs::TilePositionData*> positionsById;

    for (const auto &position : positions) {
        if (!positionsById.emplace(position.tileId, &position).second) {
            throw std::invalid_argument(
                "Position dupliquee : " + position.tileId
            );
        }
    }

    // 2. Construire les couches.
    QVector<ImageLayer> layers;
    std::set<std::string> loadedIds;

    for (const auto &tile : tiles) {

        if (!tile) {
            throw std::invalid_argument("Tuile nulle");
        }

        // Extraire l'identifiant depuis le nom du TIFF source.
        int row = 0;
        int column = 0;
        std::string tileName;

        if (!GraphCutNs::ImagePretreatmentClass::parseTileName(
                tile->getSource().string(),
                row,
                column,
                tileName)) {
            throw std::invalid_argument(
                "Nom de tuile invalide : " + tile->getSource().string()
            );
        }

        // Empêcher les identifiants dupliqués.
        if (!loadedIds.insert(tileName).second) {
            throw std::invalid_argument(
                "Tuile dupliquee : " + tileName
            );
        }

        // Retrouver sa position absolue.
        auto it = positionsById.find(tileName);

        if (it == positionsById.end()) {
            throw std::runtime_error(
                "Position introuvable : " + tileName
            );
        }

        // Charger l'aperçu basse résolution.
        layers.push_back(
            loadLowResolutionLayer(*tile, *it->second, level)
        );
    }

    // Vérifier qu'aucune position ne reste sans tuile.
    if (loadedIds.size() != positionsById.size()) {
        throw std::runtime_error(
            "Certaines positions ne correspondent a aucune tuile"
        );
    }

    return layers;
}
