#include "steps/GraphCutStep.h"
#include "core/Config.h"
#include "core/Tuile.h"
#include "algorithms/seamline/graphcut/ImagePretreatment.h"
#include <iostream>

namespace stitching::steps {
    GraphCutStep::GraphCutStep(const GraphCutInput& in) : input(in) { // J'ai imité la structure de ImportStep (Anthony avait dit qu'elle était ok)
        StepConfig cfg;
        cfg.name = "graphcut";
        cfg.options = {{"float", "scale", in.scale}};
        cfg.pipelineOptions = {{"Tiles", "tiles"}, {"Positions", "positions"}}; 
        cfg.signature.pipelineOptions = {{"Territories", "territories"}};
        config = cfg;
    }
explicit GraphCutStep(const GraphCutInput& input = {});
bool GraphCutStep::execute() {
    // Entrées : le DataStore (pipeline) si présent, sinon la structure input (appel direct)
    std::vector<std::string> tile_paths = input.tile_paths;
    std::vector<TilePositionData> positions = input.positions;
    double scale = input.scale;
    try {
        if (data && data->has("tiles")) { // On vérifie si le pointeur data vers DataStore existe, et si oui, s'il existe tiles comme clé dans DataStore
            tile_paths.clear(); // Du coup, on supprime ce qu'on a rempli pour plutot utiliser la config du pipeline  
            for (const auto& t : data->get<Tiles>("tiles")) // Pour chaque tuile du dataStore, on garde le chemin vers la tuile
                tile_paths.push_back(t->getSource().string());//On ajoute le chemin dans la liste des tuiles.
        }
        if (data && data->has("positions"))
            positions = data->get<std::vector<TilePositionData>>("positions");
        scale = option("scale").get<double>();
    } catch (const std::exception& e) {
        std::cerr << "Erreur de lecture des entrées : " << e.what() << std::endl;
        outputData["error"] = std::string("Lecture des entrées impossible : ") + e.what();
        return false;
    }

    if (tile_paths.empty()) {
        std::cerr << "Erreur : aucune tuile fournie pour le step GraphCut." << std::endl;
        outputData["error"] = "Aucune tuile fournie pour le step GraphCut.";
        return false;
    }
    if (tile_paths.size() > 254){
        std::cerr<< "Erreur : Limite maximum de tuiles 254 pour le step GraphCut." << std::endl;
        outputData["error"] = "Limite maximum de tuiles 254 pour le step GraphCut.";
        return false;
    }
    if (positions.size() != tile_paths.size()) {
        std::cerr << "Erreur : le nombre de positions ne correspond pas au nombre de tuiles." << std::endl;
        outputData["error"] = "Le nombre de positions ne correspond pas au nombre de tuiles.";
        return false;
    }
    if (scale <= 0.0 || scale > 1.0) {
        std::cerr << "Erreur : le facteur de réduction doit être compris entre 0 et 1." << std::endl;
        outputData["error"] = "Le facteur de réduction doit être compris entre 0 et 1.";
        return false;
    }

    // Appel à l'algorithme de GraphCut
    Seamline::GraphCut graphcut;
    std::vector<cv::UMat> masks_umat_small;
    std::vector<cv::Point> original_corners, corners_small;
    std::vector<cv::Size> tile_sizes;
    Seamline::ImagePretreatment pretreatment;
    cv::Mat index_matrix;
    if (pretreatment.processImages(positions, tile_paths, scale, tile_sizes, masks_umat_small, original_corners, corners_small) < 0) {
        std::cerr << "Erreur lors du prétraitement des images." << std::endl;
        outputData["error"] = "Erreur lors du prétraitement des images.";
        return false;
    }
    if (graphcut.computeSeamline(tile_paths, tile_sizes, original_corners, scale, corners_small, masks_umat_small, index_matrix) < 0) {
        std::cerr << "Echec du calcul du GraphCut." << std::endl;
        outputData["error"] = "Echec du calcul du GraphCut.";
        return false;
    }

    // Extraction des territoires
    output.tiles = graphcut.extractPolygons(index_matrix, static_cast<int>(tile_paths.size()));
    if (data) data->set("territories", output.tiles);
    outputData["territories"] = output.tiles.size();
    return true;
}
}
