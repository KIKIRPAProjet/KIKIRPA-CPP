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
    Mat raw = imread(path, IMREAD_UNCHANGED);
    if (raw.empty()) return raw;
    if (raw.channels() == 4) cvtColor(raw, raw, COLOR_BGRA2BGR);
    else if (raw.channels() == 2) { vector<Mat> ch; split(raw, ch); raw = ch[0]; }
    if (raw.channels() == 1) cvtColor(raw, raw, COLOR_GRAY2BGR);
    Mat out;
    switch (raw.depth()) {
        case CV_16U: out = raw; break;
        case CV_8U:  raw.convertTo(out, CV_16UC3, 257.0); break;       // 255 -> 65535
        case CV_16S: raw.convertTo(out, CV_16UC3, 2.0); break;
        default:     raw.convertTo(out, CV_16UC3, 65535.0); break;     // flottant 0–1
    }
    return out;
}

Mat ImagePretreatment::readSmallFloat(const string& path, double scale) {
    Mat img = read16(path);
    if (img.empty()) return img;
    Mat small16 = img;
    if (scale < 1.0) resize(img, small16, Size(), scale, scale, INTER_LINEAR);   // facteur fixe, depuis les 16 bits
    img.release();
    Mat f;
    small16.convertTo(f, CV_32FC3, 255.0 / 65535.0);                              // 0–65535 -> 0–255
    return f;
}

// ------------------------------------------------------------------ nom de tuile

bool ImagePretreatment::parseTileName(const string& path, int& row, int& col, string& id) {
    static const regex pattern_rc("_r(\\d+)_c(\\d+)\\.tiff?$", regex::icase);
    static const regex pattern_cr("_[cC](\\d+)_[rR](\\d+)\\.tiff?$", regex::icase);
    string file_name = path;
    size_t last_slash = file_name.find_last_of("/\\");
    if (last_slash != string::npos) file_name = file_name.substr(last_slash + 1);

    smatch matches;
    if (regex_search(file_name, matches, pattern_rc) && matches.size() > 2) {
        row = stoi(matches[1].str());
        col = stoi(matches[2].str());
    } else if (regex_search(file_name, matches, pattern_cr) && matches.size() > 2) {
        col = stoi(matches[1].str());
        row = stoi(matches[2].str());
    } else {
        return false;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "r%02d_c%02d", row, col);
    id = buf;
    return true;
}

// ------------------------------------------------------------------ chargement

int ImagePretreatment::processImages(const vector<TilePositionData>& input_positions,
                                     const vector<string>& tile_paths,
                                     double scale,
                                     vector<Size>& tile_sizes,
                                     vector<UMat>& masks_umat_small,
                                     vector<Point>& original_corners,
                                     vector<Point>& corners_small) {
    if (!(scale > 0.0 && scale <= 1.0)) {
        cout << "Scale expected below 1.0 : " << scale << endl;
        return -1;
    }
    
    // Créer une map temporaire à partir de la structure pour une recherche rapide
    map<string, Point2d> positions_map;
    for (const auto& tp : input_positions) {
        positions_map[tp.tile_id] = tp.position;
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
            full_size = img.size();

            const Size small_size(saturate_cast<int>(full_size.width * scale), saturate_cast<int>(full_size.height * scale));
            if (small_size.width < 1 || small_size.height < 1) {
                cout << "tile quality too low : " << img_path << endl;
                return -1;
            }
            masks_umat_small.push_back(UMat(small_size, CV_8U, Scalar(255)));
        }   // <- img (pleine résolution) libérée ici
        tile_sizes.push_back(full_size);

        int row = 0, col = 0;
        string id;
        if (!parseTileName(img_path, row, col, id)) {
            cout << "wrong tile name: " << img_path << endl;
            return -1;
        }

        // Recherche dans la structure d'input
        auto p = positions_map.find(id);
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
        corners_small.push_back(Point(cvFloor(original_corners[i].x * scale), cvFloor(original_corners[i].y * scale)));
        
    return 1;
}

}  // namespace Seamline