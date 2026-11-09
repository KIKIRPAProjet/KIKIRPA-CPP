// src/backend/steps/fusion_step.cpp
#include "steps/fusion_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/intensity_correction.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"

#include <opencv2/core/fast_math.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace StepsNs {

namespace {
// Valeur maximale pour un pixel actif dans un masque binaire 8-bits OpenCV.
constexpr int VALID_MASK_VALUE = 255;

// Tolérance angulaire en radians : à ce stade du pipeline, seules les pures
// translations sont prises en charge lors de la recomposition de la mosaïque.
constexpr double ROTATION_TOLERANCE = 1e-9;

// Seuil minimal de gain pour éviter une division par zéro lors de l'application
// de la correction photométrique inverse.
constexpr double MIN_GAIN_THRESHOLD = 1e-6;
} // namespace

// -----------------------------------------------------------------------------
// Constructeur : déclaration du contrat de step
// -----------------------------------------------------------------------------
FusionStepClass::FusionStepClass(const FusionInput& initialInput)
    : input(initialInput) {
    ConfigNs::StepConfig stepConfig;
    stepConfig.name = "fusion";

    // Déclaration des paramètres configurables (avec leurs valeurs par défaut)
    stepConfig.parameters = {
        {"int", "blockSize", initialInput.blockSize},
        {"string", "outputPath", initialInput.outputPath},
        {"bool", "enableBlending", initialInput.enableBlending}
    };

    // Données d'entrée obligatoires dans le DataStore
    stepConfig.inputs = {
        {"Tiles", "tiles"},
        {"Positions", "positions"},
        {"Territories", "territories"}
    };

    // Donnée de sortie générée dans le DataStore
    stepConfig.outputs = {
        {"CompositeBlocks", "compositeBlocks"}
    };

    configJson = stepConfig;
}

// -----------------------------------------------------------------------------
// Calcul de la boîte englobante de l'ensemble des tuiles
// -----------------------------------------------------------------------------
cv::Rect FusionStepClass::computeGlobalBoundingBox(
    const std::vector<TilePlacement>& placements) {
    if (placements.empty()) return {};

    // L'union de rectangles avec l'opérateur '|' d'OpenCV calcule le plus petit
    // rectangle contenant les deux rectangles opérandes.
    cv::Rect result = placements.front().globalBox;
    for (std::size_t i = 1; i < placements.size(); ++i) {
        result |= placements[i].globalBox;
    }
    return result;
}

// -----------------------------------------------------------------------------
// Rasterisation du territoire d'une tuile dans le repère local d'un bloc
// -----------------------------------------------------------------------------
cv::Mat FusionStepClass::makeTerritoryMask(
    const cv::Rect& block, const DataNs::TileTerritory& territory) {
    // Initialise un masque binaire noir (0 = non couvert) de la taille du bloc
    cv::Mat mask(block.size(), CV_8UC1, cv::Scalar(0));

    // Parcourt tous les polygones composant le territoire de cette tuile
    for (std::size_t p = 0; p < territory.polygons.size(); ++p) {
        const auto& outer = territory.polygons[p];
        if (outer.size() < 3) continue;

        // Translation des coordonnées du polygone du repère global vers le repère local du bloc
        std::vector<cv::Point> shifted;
        shifted.reserve(outer.size());
        for (const auto& point : outer) {
            shifted.emplace_back(point.x - block.x, point.y - block.y);
        }

        // Remplissage du polygone extérieur avec la valeur 255 (pixel valide)
        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{shifted}, cv::Scalar(VALID_MASK_VALUE));

        // Découpe des trous intérieurs éventuels (holes) en les repeignant en 0
        if (p < territory.holes.size()) {
            for (const auto& hole : territory.holes[p]) {
                if (hole.size() < 3) continue;
                std::vector<cv::Point> shiftedHole;
                shiftedHole.reserve(hole.size());
                for (const auto& point : hole) {
                    shiftedHole.emplace_back(point.x - block.x, point.y - block.y);
                }
                cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{shiftedHole}, cv::Scalar(0));
            }
        }
    }
    return mask;
}

// -----------------------------------------------------------------------------
// Rendu d'un bloc composite individuel
// -----------------------------------------------------------------------------
void FusionStepClass::renderBlock(
    const cv::Rect& block,
    const std::vector<TilePlacement>& placements,
    const std::vector<DataNs::TileTerritory>& territories,
    cv::Mat& outBlockRgb,
    cv::Mat& outBlockProvenance,
    cv::Mat& outBlockMask) const {

    // Initialisation des trois couches du bloc aux dimensions demandées :
    // - RGB : 16 bits non-signé 3 canaux (CV_16UC3) pour haute dynamique couleur
    // - Provenance : 16 bits non-signé (CV_16UC1), stocke l'ID de la tuile source (+1)
    // - Masque : 8 bits mono-canal (CV_8UC1), 255 = pixel valide dans la mosaïque
    outBlockRgb = cv::Mat::zeros(block.size(), CV_16UC3);
    outBlockProvenance = cv::Mat::zeros(block.size(), CV_16UC1);
    outBlockMask = cv::Mat::zeros(block.size(), CV_8UC1);

    // Indexation rapide des territoires par index de tuile
    std::map<int, const DataNs::TileTerritory*> territoryByIndex;
    for (const auto& territory : territories) {
        territoryByIndex[territory.tileIndex] = &territory;
    }

    // Parcourt les tuiles candidates pour vérifier si elles chevauchent le bloc
    for (const auto& tile : placements) {
        auto territoryIt = territoryByIndex.find(tile.index);
        if (territoryIt == territoryByIndex.end()) continue;

        // Intersection géométrique entre le bloc courant et la boîte de la tuile
        const cv::Rect intersection = block & tile.globalBox;
        if (intersection.empty()) continue; // Aucun pixel commun

        // Lecture de l'image de la tuile depuis le disque sans modification de format
        cv::Mat source = cv::imread(tile.path.string(), cv::IMREAD_UNCHANGED);
        if (source.empty()) {
            throw std::runtime_error("Impossible de lire la tuile : " + tile.path.string());
        }

        // Homogénéisation en CV_16UC3 pour un traitement uniforme :
        // Le facteur 257.0 mappe [0, 255] exactement sur [0, 65535] (255 * 257 = 65535).
        if (source.type() == CV_8UC3) {
            source.convertTo(source, CV_16UC3, 257.0);
        } else if (source.type() == CV_8UC1) {
            cv::cvtColor(source, source, cv::COLOR_GRAY2BGR);
            source.convertTo(source, CV_16UC3, 257.0);
        } else if (source.type() == CV_16UC1) {
            cv::cvtColor(source, source, cv::COLOR_GRAY2BGR);
        }

        if (source.type() != CV_16UC3) {
            throw std::runtime_error("Tuile non convertible en CV_16UC3 : " + tile.path.string());
        }
        if (source.size() != tile.globalBox.size()) {
            throw std::runtime_error("Dimensions inattendues pour la tuile : " + tile.path.string());
        }

        // Régions d'intérêt (ROI) :
        // - srcRect : coordonnées relatives dans l'image de la tuile
        // - dstRect : coordonnées relatives dans l'image du bloc de sortie
        const cv::Rect srcRect(intersection.x - tile.globalBox.x,
                               intersection.y - tile.globalBox.y,
                               intersection.width, intersection.height);
        const cv::Rect dstRect(intersection.x - block.x,
                               intersection.y - block.y,
                               intersection.width, intersection.height);

        // Rasterise le territoire de la tuile et en extrait la zone correspondant au rectangle cible
        cv::Mat territoryMask = makeTerritoryMask(block, *territoryIt->second);
        cv::Mat maskRoi = territoryMask(dstRect);

        // Application de la correction de gain photométrique sur les pixels de la tuile
        cv::Mat pixels = source(srcRect);
        cv::Mat corrected;
        pixels.convertTo(corrected, CV_16UC3, tile.lightGain);

        // Copie des pixels corrigés dans le bloc de sortie sous le contrôle du masque de territoire
        corrected.copyTo(outBlockRgb(dstRect), maskRoi);

        // Marque l'indice de la tuile source (1-indexé pour réserver 0 à l'arrière-plan vide)
        outBlockProvenance(dstRect).setTo(cv::Scalar(tile.index + 1), maskRoi);

        // Marque les pixels comme valides dans le masque d'opacité
        outBlockMask(dstRect).setTo(cv::Scalar(VALID_MASK_VALUE), maskRoi);
    }
}

// -----------------------------------------------------------------------------
// Méthode principale d'exécution du step
// -----------------------------------------------------------------------------
bool FusionStepClass::execute() {
    output = {};
    result = Json::object();
    try {
        // 1. Vérification de la disponibilité du DataStore et de ses dépendances
        if (!dataStore) {
            throw std::runtime_error("FusionStep : DataStore non initialise");
        }
        if (!dataStore->has("tiles") || !dataStore->has("positions") ||
            !dataStore->has("territories")) {
            throw std::runtime_error("FusionStep : tiles, positions ou territories manquant");
        }

        // 2. Lecture des paramètres configurés (avec priorité aux paramètres du JSON)
        int blockSize = input.blockSize;
        std::string outputPath = input.outputPath;
        bool enableBlending = input.enableBlending;
        if (hasParameter("blockSize")) blockSize = getParameter("blockSize").get<int>();
        if (hasParameter("outputPath")) outputPath = getParameter("outputPath").get<std::string>();
        if (hasParameter("enableBlending")) enableBlending = getParameter("enableBlending").get<bool>();

        if (blockSize <= 0) {
            throw std::runtime_error("FusionStep : blockSize doit etre positif");
        }
        if (enableBlending) {
            throw std::runtime_error("FusionStep : blending pas encore implemente");
        }

        // 3. Récupération des données d'entrée
        const auto& tiles = dataStore->get<DataNs::TileList>("tiles");
        const auto& positions =
            dataStore->get<std::vector<DataNs::TilePositionData>>("positions");
        const auto& territories =
            dataStore->get<std::vector<DataNs::TileTerritory>>("territories");

        if (tiles.empty() || positions.size() != tiles.size()) {
            throw std::runtime_error("FusionStep : nombre de tuiles/positions invalide");
        }
        if (territories.empty()) {
            throw std::runtime_error("FusionStep : aucun territoire");
        }

        // 4. Calcul de l'offset minimal pour recaler le coin haut-gauche sur (0, 0),
        // en parfaite cohérence avec le repère utilisé par GraphCut.
        double minX = 1e300, minY = 1e300;
        for (const auto& pos : positions) {
            minX = std::min(minX, pos.position.x);
            minY = std::min(minY, pos.position.y);
        }
        const double offsetX = std::floor(minX);
        const double offsetY = std::floor(minY);

        // 5. Lecture optionnelle des corrections de luminosité (IntensityCorrection)
        // Les gains enregistrés par intensity-correction mesurent la luminance relative ;
        // pour corriger la tuile, on multiplie par l'inverse du gain estimé.
        std::vector<double> tileGains(tiles.size(), 1.0);
        if (dataStore->has("intensityCorrection")) {
            const auto& ic = dataStore->get<DataNs::IntensityCorrection>("intensityCorrection");
            for (std::size_t i = 0; i < tiles.size() && i < ic.tileGains.size(); ++i) {
                if (ic.tileGains[i] > MIN_GAIN_THRESHOLD) {
                    tileGains[i] = 1.0 / ic.tileGains[i];
                }
            }
        }

        // 6. Association de chaque tuile à sa position globale
        std::vector<TilePlacement> placements;
        placements.reserve(tiles.size());
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            const auto& tile = tiles[i];
            if (!tile) throw std::runtime_error("FusionStep : tuile nulle");

            const std::string filename = tile->getSource().filename().string();
            const DataNs::TilePositionData* position = nullptr;

            // Recherche de la position correspondante par correspondance d'identifiant ou de nom
            for (const auto& candidate : positions) {
                if (filename.find(candidate.tileId) != std::string::npos ||
                    candidate.tileId == std::to_string(tile->getId())) {
                    position = &candidate;
                    break;
                }
            }
            // Fallback par correspondance d'indice si non trouvé par ID
            if (!position && i < positions.size()) {
                position = &positions[i];
            }
            if (!position) {
                throw std::runtime_error("FusionStep : position introuvable pour " + filename);
            }
            if (std::abs(position->rotationRadians) > ROTATION_TOLERANCE) {
                throw std::runtime_error("FusionStep : rotations non prises en charge");
            }

            const auto& level = tile->getLevel(0);
            if (level.width <= 0 || level.height <= 0) {
                throw std::runtime_error("FusionStep : dimensions invalides");
            }

            placements.push_back(TilePlacement{
                static_cast<int>(i),
                tile->getSource(),
                cv::Rect(cvRound(position->position.x - offsetX),
                         cvRound(position->position.y - offsetY),
                         level.width, level.height),
                tileGains[i]
            });
        }

        // Validation des indices de territoire
        for (const auto& territory : territories) {
            if (territory.tileIndex < 0 ||
                territory.tileIndex >= static_cast<int>(tiles.size())) {
                throw std::runtime_error("FusionStep : tileIndex hors limites");
            }
        }

        // 7. Calcul du canevas global englobant toutes les tuiles
        const cv::Rect bounds = computeGlobalBoundingBox(placements);
        if (bounds.empty()) {
            throw std::runtime_error("FusionStep : mosaique vide");
        }

        // Création du répertoire de sortie si une sauvegarde sur disque est demandée
        std::filesystem::path outputDir;
        if (!outputPath.empty()) {
            outputDir = outputPath;
            std::filesystem::create_directories(outputDir);
        }

        // 8. Découpage régulier en blocs carrés (blockSize x blockSize) et rendu
        DataNs::CompositeBlockList compositeBlocks;

        for (int y = bounds.y; y < bounds.y + bounds.height; y += blockSize) {
            for (int x = bounds.x; x < bounds.x + bounds.width; x += blockSize) {
                // Délimitation du bloc (rogné sur les bords droits et bas de la mosaïque)
                const cv::Rect block(x, y,
                    std::min(blockSize, bounds.x + bounds.width - x),
                    std::min(blockSize, bounds.y + bounds.height - y));

                cv::Mat rgb, provenance, mask;
                renderBlock(block, placements, territories, rgb, provenance, mask);

                // Sauvegarde optionnelle sur disque des couches intermédiaires (PNG)
                if (!outputDir.empty()) {
                    const std::string suffix = "_x" + std::to_string(x) +
                                               "_y" + std::to_string(y) + ".png";
                    cv::imwrite((outputDir / ("rgb" + suffix)).string(), rgb);
                    cv::imwrite((outputDir / ("provenance" + suffix)).string(), provenance);
                    cv::imwrite((outputDir / ("mask" + suffix)).string(), mask);
                }

                // Construction de la structure CompositeBlock en mémoire
                DataNs::CompositeBlock compBlock;
                compBlock.imageRgb = rgb;
                compBlock.provenance = provenance;
                compBlock.mask = mask;
                compBlock.blockRect = block;
                compositeBlocks.push_back(std::move(compBlock));
                ++output.totalBlocks;
            }
        }

        // 9. Publication des blocs composites dans le DataStore
        dataStore->replace("compositeBlocks", compositeBlocks);

        output.mosaicPath = outputPath;
        result["mosaicPath"] = output.mosaicPath;
        result["totalBlocks"] = output.totalBlocks;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Erreur FusionStep : " << e.what() << '\n';
        result["error"] = e.what();
        return false;
    }
}

} // namespace StepsNs
