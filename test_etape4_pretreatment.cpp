#include <iostream>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>
#include "algorithms/stitching/positioning/TilePositioner.h"
#include "algorithms/seamline/graphcut/ImagePretreatment.h"

int main() {
    std::vector<std::string> paths = {
        "images/img_C00_R00.tif",
        "images/img_C01_R00.tif",
        "images/img_C00_R01.tif",
        "images/img_C01_R01.tif"
    };

    stitching::positioning::PositioningSettings settings;
    settings.overlap = 0.25f;
    std::vector<stitching::TilePositionData> positions;
    stitching::positioning::computeTilePositions(paths, settings, positions, std::cout);

    std::cout << "\n========================================" << std::endl;
    std::cout << "[TEST ETAPE 4] Transition & Pretreatment" << std::endl;
    std::cout << "========================================" << std::endl;

    double scale = 0.2;
    std::vector<cv::Size> tile_sizes;
    std::vector<cv::UMat> masks_umat_small;
    std::vector<cv::Point> original_corners;
    std::vector<cv::Point> corners_small;

    Seamline::ImagePretreatment ip;
    int ret = ip.processImages(positions, paths, scale, tile_sizes, masks_umat_small, original_corners, corners_small);

    if (ret <= 0) {
        std::cerr << "[-] Echec de processImages (code " << ret << ")." << std::endl;
        return 1;
    }

    std::cout << "[+] Pretraitement reussi !" << std::endl;
    for (size_t i = 0; i < original_corners.size(); ++i) {
        std::cout << "    Tuile " << i << " (" << paths[i] << ") -> Coin : ("
                  << original_corners[i].x << ", " << original_corners[i].y << ") px"
                  << " | Dim : " << tile_sizes[i].width << "x" << tile_sizes[i].height
                  << " | Coin reduit (x0.2) : (" << corners_small[i].x << ", " << corners_small[i].y << ") px"
                  << std::endl;
    }
    return 0;
}
