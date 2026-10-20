#include <iostream>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>
#include "algorithms/stitching/positioning/TilePositioner.h"
#include "algorithms/seamline/graphcut/ImagePretreatment.h"
#include "algorithms/seamline/graphcut/GraphCutSeamFinder.h"

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "[1] Execution Positioning (Etape 3)..." << std::endl;
    std::cout << "========================================" << std::endl;

    std::vector<std::string> paths = {
        "images/img_C00_R00.tif",
        "images/img_C01_R00.tif",
        "images/img_C00_R01.tif",
        "images/img_C01_R01.tif"
    };

    stitching::positioning::PositioningSettings settings;
    settings.overlap = 0.25f;
    std::vector<stitching::TilePositionData> positions;
    if (!stitching::positioning::computeTilePositions(paths, settings, positions, std::cout)) {
        std::cerr << "[-] Echec lors du positionnement global." << std::endl;
        return 1;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "[2] Transition vers Pretreatment (Etape 4)..." << std::endl;
    std::cout << "========================================" << std::endl;

    double scale = 0.2;
    std::vector<cv::Size> tile_sizes;
    std::vector<cv::UMat> masks_umat_small;
    std::vector<cv::Point> original_corners;
    std::vector<cv::Point> corners_small;

    Seamline::ImagePretreatment ip;
    int ret = ip.processImages(
        positions, paths, scale, tile_sizes, masks_umat_small, original_corners, corners_small
    );

    // Dans le code de ImagePretreatment: 1 = succes, -1 = erreur
    if (ret <= 0) {
        std::cerr << "[-] Echec de processImages (code " << ret << ")." << std::endl;
        return 1;
    }

    std::cout << "[+] Pretraitement reussi !" << std::endl;
    for (size_t i = 0; i < original_corners.size(); ++i) {
        std::cout << "    Tuile " << i << " (" << paths[i] << ") -> Coin : ("
                  << original_corners[i].x << ", " << original_corners[i].y << ") px"
                  << " | Dim : " << tile_sizes[i].width << "x" << tile_sizes[i].height << std::endl;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "[3] Execution GraphCut & Blending (Etape 5)..." << std::endl;
    std::cout << "========================================" << std::endl;

    Seamline::GraphCut gc;
    cv::Mat final_mosaic;
    cv::Mat index_matrix;

    int status = gc.computeSeamline(
        paths, tile_sizes, original_corners, scale, corners_small, masks_umat_small, final_mosaic, index_matrix
    );

    if (status != 1 || final_mosaic.empty()) {
        std::cerr << "[-] Echec lors de computeSeamline (code " << status << ")." << std::endl;
        return 1;
    }

    std::cout << "[+] Mosaique assemblee avec succes !" << std::endl;
    std::cout << "    Dimensions finales : " << final_mosaic.cols << " x " << final_mosaic.rows << " px" << std::endl;

    std::string out_name = "mosaique_reelle.png";
    if (cv::imwrite(out_name, final_mosaic)) {
        std::cout << "[OK] Image sauvegardee avec succes : " << out_name << std::endl;
    } else {
        std::cerr << "[-] Erreur d'ecriture sur le disque." << std::endl;
        return 1;
    }

    return 0;
}
