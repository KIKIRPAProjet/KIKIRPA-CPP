// src/backend/steps/graph_cut_step.cpp
#include "steps/graph_cut_step.h"

#include "algorithms/seamline/graphcut/ImagePretreatment.h"
#include "core/config/pipeline_config.h"
#include "core/data/tile.h"

#include <opencv2/core.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace StepsNs {

    // Déclare l'identité du step : son nom, son paramètre `scale` (valeur par défaut = celle de l'entrée), les données
    // qu'il lit (tuiles et positions) et celle qu'il produit (territoires). Le pipeline s'en sert pour vérifier l'enchaînement.
    GraphCutStepClass::GraphCutStepClass(const GraphCutInput& initialInput) : input(initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "graphcut";
        stepConfig.parameters = {{"float", "scale", initialInput.scale}};
        stepConfig.inputs = {{"Tiles", "tiles"}, {"Positions", "positions"}};
        stepConfig.outputs = {{"Territories", "territories"}};
        configJson = stepConfig;
    }

    bool GraphCutStepClass::execute() {
        // 1. Lecture des entrées. Par défaut celles de `input` (appel direct) ; dans un pipeline, les données du
        // DataStoreClass les remplacent. Le facteur de réduction est toujours relu dans la configuration, pour tenir
        // compte d'un JSON chargé après la construction.
        std::vector<std::string> tilePaths = input.tilePaths;
        std::vector<DataNs::TilePositionData> positions = input.positions;
        double scale = input.scale;
        try {
            if (dataStore && dataStore->has("tiles")) {
                tilePaths.clear();
                for (const auto& tile : dataStore->get<DataNs::TileList>("tiles"))
                    tilePaths.push_back(tile->getSource().string());   // l'algorithme relit les images depuis les fichiers d'origine
            }
            if (dataStore && dataStore->has("positions"))
                positions = dataStore->get<std::vector<DataNs::TilePositionData>>("positions");
            scale = getParameter("scale").get<double>();
        } catch (const std::exception& exception) {
            std::cerr << "Erreur de lecture des entrées : " << exception.what() << std::endl;
            result["error"] = std::string("Lecture des entrées impossible : ") + exception.what();
            return false;
        }

        // 2. Contrôles : au moins une tuile, une position par tuile, facteur de réduction valide.
        if (tilePaths.empty()) {
            std::cerr << "Erreur : aucune tuile fournie pour le step GraphCut." << std::endl;
            result["error"] = "Aucune tuile fournie pour le step GraphCut.";
            return false;
        }
        if (positions.size() != tilePaths.size()) {
            std::cerr << "Erreur : le nombre de positions ne correspond pas au nombre de tuiles." << std::endl;
            result["error"] = "Le nombre de positions ne correspond pas au nombre de tuiles.";
            return false;
        }
        if (scale <= 0.0 || scale > 1.0) {
            std::cerr << "Erreur : le facteur de réduction doit être compris entre 0 et 1." << std::endl;
            result["error"] = "Le facteur de réduction doit être compris entre 0 et 1.";
            return false;
        }

        // 3. Prétraitement : tailles des tuiles, coins dans la mosaïque (pleine résolution et réduits) et masques réduits.
        Seamline::GraphCut graphCut;
        std::vector<cv::UMat> masksUmatSmall;
        std::vector<cv::Point> originalCorners, cornersSmall;
        std::vector<cv::Size> tileSizes;
        Seamline::ImagePretreatment imagePretreatment;
        cv::Mat finalMosaic;
        cv::Mat indexMatrix;
        if (imagePretreatment.processImages(positions, tilePaths, scale, tileSizes, masksUmatSmall, originalCorners, cornersSmall) < 0) {
            std::cerr << "Erreur lors du prétraitement des images." << std::endl;
            result["error"] = "Erreur lors du prétraitement des images.";
            return false;
        }

        // 4. Coupe de graphe : produit la carte de provenance `indexMatrix` (la tuile choisie pour chaque pixel de la
        // mosaïque) et une mosaïque d'aperçu (`finalMosaic`, non utilisée ici).
        if (graphCut.computeSeamline(tilePaths, tileSizes, originalCorners, scale, cornersSmall, masksUmatSmall, finalMosaic, indexMatrix) < 0) {
            std::cerr << "Echec du calcul du GraphCut." << std::endl;
            result["error"] = "Echec du calcul du GraphCut.";
            return false;
        }

        // 5. Territoires : les polygones de chaque tuile sont extraits de la carte de provenance, puis rangés dans le
        // DataStoreClass pour les steps suivants.
        output.territories = graphCut.extractPolygons(indexMatrix, static_cast<int>(tilePaths.size()));
        if (dataStore) dataStore->replace("territories", output.territories);
        result["territories"] = output.territories.size();
        return true;
    }

}
