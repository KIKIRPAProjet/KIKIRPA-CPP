#pragma once

#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

namespace Seamline {

    class imagePretreatment {
    public:
        static int processImages(float overlap, 
                                 int argc, 
                                 char* argv[], 
                                 std::vector<cv::Mat>& original_images, 
                                 std::vector<cv::UMat>& images_umat_small, 
                                 std::vector<cv::UMat>& masks_umat_small, 
                                 std::vector<cv::Point>& original_corners, 
                                 std::vector<cv::Point>& corners_small, 
                                 cv::Size& common_size);
    };

} // namespace Seamline
