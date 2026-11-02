#include "algorithms/seamline/graphcut/ImagePretreatment.h"
#include <cmath>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <map>

using namespace std;
using namespace cv;

namespace Seamline {

// ------------------------------------------------------------------ lecture sans perte

Mat ImagePretreatment::read16(const string& path) {
    Mat raw = imread(path, IMREAD_UNCHANGED); // loads the image form path, Mat holds the image's pixels
    if (raw.empty()) return raw; // returns an empty image if the loading fails
    if (raw.channels() == 4) cvtColor(raw, raw, COLOR_BGRA2BGR); //4channels : blue, green, red, transparency -> remove transparency
    else if (raw.channels() == 2) { vector<Mat> ch; split(raw, ch); raw = ch[0]; } //2 channels -> keeps the first channel
    if (raw.channels() == 1) cvtColor(raw, raw, COLOR_GRAY2BGR); //1 channel : grayscale -> converts it to three identical channels
    Mat out; 
    // converts pixel values to unsigned 16-bit integers, form 0 to 65535
    switch (raw.depth()) {
        case CV_16U: out = raw; break;
        case CV_8U:  raw.convertTo(out, CV_16UC3, 257.0); break;       // 255 -> 65535
        case CV_16S: raw.convertTo(out, CV_16UC3, 2.0); break;
        default:     raw.convertTo(out, CV_16UC3, 65535.0); break;     // flottant 0–1
    } // CV_16UC3 = 16-bit unsigned values with 3 channels
    return out; // result = image in a consistent format for later
}

Mat ImagePretreatment::readSmallFloat(const string& path, double scale) { // create a smaller image for calculation
    Mat img = read16(path);
    if (img.empty()) return img;
    Mat small16 = img;
    // scale < 1 => resizes the image
    if (scale < 1.0) resize(img, small16, Size(), scale, scale, INTER_LINEAR);   // facteur fixe, depuis les 16 bits
    img.release();
    Mat f;
    small16.convertTo(f, CV_32FC3, 255.0 / 65535.0);   //converts pixel value to floating-point numbers -> 0-65535 to 0-255
    return f;
}

// ------------------------------------------------------------------ nom de tuile

bool ImagePretreatment::parseTileName(const string& path, int& row, int& col, string& id) {
    static const regex pattern("_r(\\d+)_c(\\d+)\\.tiff?$", regex::icase); // expect a filename such as : photo_r3_c7.tif or .tiff
    string file_name = path;
    size_t last_slash = file_name.find_last_of("/\\");
    if (last_slash != string::npos) file_name = file_name.substr(last_slash + 1);

    smatch matches;
    if (!regex_search(file_name, matches, pattern) || matches.size() <= 2) return false;
    row = stoi(matches[1].str()); //extracts row from the filename
    col = stoi(matches[2].str()); //extracts column from the filename

    char buf[32];
    snprintf(buf, sizeof(buf), "r%02d_c%02d", row, col); //%02d = write the number using at least two digits, adding a leading zero if needed.
    id = buf;
    return true;
}

// ------------------------------------------------------------------ chargement

int ImagePretreatment::processImages(//Input
                                    const vector<TilePositionData>& input_positions, //tile position, associated with tile identifier
                                     const vector<string>& tile_paths, //path to image file
                                     double scale, // 	Reduction factor 0<x<1
                                    //Output, & = lists are passed by reference
                                     vector<Size>& tile_sizes, //Original image dimensions
                                     vector<UMat>& masks_umat_small, //mask for each image at the reduced size
                                     vector<Point>& original_corners, //tile positions in full-resolution masaic
                                     vector<Point>& corners_small)//tile positions in reduced masaic
    {
    if (!(scale > 0.0 && scale <= 1.0)) { // if the scale is invalid -> returns -1
        cout << "Scale expected below 1.0 : " << scale << endl;
        return -1;
    }
    
    // Créer une map temporaire à partir de la structure pour une recherche rapide
    map<string, Point2d> positions_map;
    for (const auto& tp : input_positions) {
        positions_map[tp.tile_id] = tp.position; //build a lookup table of positions
    }

    vector<Point2d> raw_corners;

    for (size_t t = 0; t < tile_paths.size(); ++t) {
        const string& img_path = tile_paths[t];
        Size full_size;
        {
            Mat img = read16(img_path);
            if (img.empty()) {
                cout << "No images found : " << img_path << endl;
                return -1;
            }
            full_size = img.size(); //function reads the image and records width and height

            const Size small_size(saturate_cast<int>(full_size.width * scale), saturate_cast<int>(full_size.height * scale));
            if (small_size.width < 1 || small_size.height < 1) {
                cout << "tile quality too low : " << img_path << endl;
                return -1;
            }
            masks_umat_small.push_back(UMat(small_size, CV_8U, Scalar(255))); //mask = wich pixels are usable
        }   // <- img (pleine résolution) libérée ici
        tile_sizes.push_back(full_size);

        int row = 0, col = 0;
        string id;
        if (!parseTileName(img_path, row, col, id)) { //extracts the identifier from filename
            cout << "wrong tile name: " << img_path << endl;
            return -1;
        }

        // Recherche dans la structure d'input
        auto p = positions_map.find(id); //looks up the corresponding position
        if (p == positions_map.end()) {
            cout << "Position missing for " << id << " in the input structure" << endl;
            return -1;
        }
        raw_corners.push_back(p->second);
    }
    
    if (tile_sizes.empty()) {
        cout << "No images found" << endl;
        return -1;
    }

    // 2. coins dans le repère de la mosaïque : arrondis au pixel, la plus petite position ramenée à 0
    double minX = 1e300, minY = 1e300;
    for (const Point2d& p : raw_corners) { minX = min(minX, p.x); minY = min(minY, p.y); }
    const double ox = floor(minX), oy = floor(minY);
    for (const Point2d& p : raw_corners) original_corners.push_back(Point(cvRound(p.x - ox), cvRound(p.y - oy)));

    // 3. coins sur la grille réduite
    for (size_t i = 0; i < original_corners.size(); ++i)
        corners_small.push_back(Point(cvFloor(original_corners[i].x * scale), cvFloor(original_corners[i].y * scale))); //calculate the reduced positions
         
    return 1;
}

}  // namespace Seamline
