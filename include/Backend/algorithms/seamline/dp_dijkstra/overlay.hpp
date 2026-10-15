// Illustration du réseau de seamlines : la mosaïque finale avec les lignes de couture et les jonctions.
//
// Usage :
//     std::map<std::string, cv::Point2d> origins;           // clé "H_r_c" / "V_r_c" → coin haut-gauche
//     origins["H_0_0"] = { 4140.0, 0.0 };                    // de SA carte de coût dans la mosaïque (px d'origine)
//     ...
//     auto seams = seamlib::solve_global_network(grid, seamlib::Method::DP, J);
//     cv::Mat img = seamlib::draw_seam_network(mosaic, grid, seams, J, origins, 0.1);
//     cv::imwrite("reseau.png", img);
#pragma once

#include <opencv2/core.hpp>

#include <map>
#include <string>

#include "algorithms/seamline/export.hpp"
#include "algorithms/seamline/junctions.hpp"

namespace seamlib {

    // Dessine sur une copie BGR de `mosaic` : chaque seamline en rouge, chaque jonction en cercle jaune.
    //   origins : position de chaque carte de coût dans la mosaïque (px d'origine). Les adjacences V sont
    //             transposées dans le solveur (x solveur = y mosaïque, y solveur = x mosaïque) ; c'est géré ici.
    //             Une adjacence sans origine est ignorée.
    //   scale   : facteur d'affichage de la sortie (ex. 0.1 pour une grande mosaïque), dans ]0, 1].
    // Les coordonnées des seamlines sont divisées par EdgeCost::scale pour revenir en pixels d'origine.
    KIRK_API cv::Mat draw_seam_network(const cv::Mat& mosaic, const SeamGrid& grid,
        const std::map<std::string, SolveResult>& seams, const JunctionSet& junctions,
        const std::map<std::string, cv::Point2d>& origins, double scale = 1.0);

}  // namespace seamlib
