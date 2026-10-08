#pragma once

#include <string>
#include <regex>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/stitching/detail/seam_finders.hpp>
#include <vector>

namespace Seamline {
    // Structure pour récupérer les polygones d'une tuile
    struct TileTerritory {
        int tile_index;
        std::vector<std::vector<cv::Point>> polygons;
    };

    class GraphCut {
        public:
            int computeSeamline(const std::vector<cv::Mat> &original_images, 
                        const std::vector<cv::Point> &original_corners, 
                        std::vector<cv::UMat> &images_umat_small,
                        std::vector<cv::Point> &corners_small, 
                        std::vector<cv::UMat> &masks_umat_small, 
                        const cv::Size &common_size, 
                        cv::Mat &final_mosaic, 
                        cv::Mat &index_matrix);
            std::vector<TileTerritory> extractPolygons(const cv::Mat &index_matrix, int num_tiles);
        private:
    };

};
