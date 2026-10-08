#include "pretreatment_seamline/ImagePretreatment.h"
#include <regex>
#include <iostream>

using namespace std;
using namespace cv;

namespace Seamline {

int imagePretreatment::processImages(float overlap, 
                                     int argc, 
                                     char* argv[], 
                                     vector<Mat>& original_images, 
                                     vector<UMat>& images_umat_small, 
                                     vector<UMat>& masks_umat_small, 
                                     vector<Point>& original_corners, 
                                     vector<Point>& corners_small, 
                                     Size& common_size) {
    
    regex pattern("_r(\\d+)_c(\\d+)\\.tif$"); 
    
    for (int i = 2; i < argc; ++i) {
        string img_path = argv[i];
        Mat img = imread(img_path, IMREAD_COLOR);

        if (img.empty()) {
            cout << "No images found : " << img_path << endl;
            return -1;
        }
        if (i == 2) common_size = img.size();

        original_images.push_back(img); 

        int W = img.cols; 
        int H = img.rows;
        
        int overlap_x = W * overlap; 
        int overlap_y = H * overlap; 
        int step_x = W - overlap_x; 
        int step_y = H - overlap_y; 

        int col = 0, row = 0;
        smatch matches; 
        
        string file_name = img_path; 
        size_t last_slash = file_name.find_last_of("/\\");
        if (last_slash != string::npos) {
            file_name = file_name.substr(last_slash + 1);
        }

        if (regex_search(file_name, matches, pattern) && matches.size() > 2) {
            row = stoi(matches[1].str()); 
            col = stoi(matches[2].str()); 
        }

        Point corner(col * step_x, row * step_y); 
        original_corners.push_back(corner); 

        Mat mask(img.size(), CV_8U, Scalar(255)); 

        float scale = 1000.0f / max(img.cols, img.rows); 
        if (scale > 1.0f) scale = 1.0f; 

        Mat img_small, mask_small; 
        resize(img, img_small, Size(), scale, scale, INTER_LINEAR); 
        resize(mask, mask_small, Size(), scale, scale, INTER_NEAREST); 

        Mat img_small_f; 
        img_small.convertTo(img_small_f, CV_32F); 

        images_umat_small.push_back(img_small_f.getUMat(ACCESS_READ)); 
        masks_umat_small.push_back(mask_small.getUMat(ACCESS_RW));  
        
        corners_small.push_back(Point(corner.x * scale, corner.y * scale));
    }
    return 1;
}

} // namespace Seamline
