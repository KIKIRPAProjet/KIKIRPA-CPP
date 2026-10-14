#include <iostream>
#include <vector>
#include <opencv2/opencv.hpp>
#include "pretreatment_seamline/ImagePretreatment.h"
#include "algorithms/seamline/GraphCutSeamFinder.h"
#include "positioning/TilePositioner.h"

using namespace std;
using namespace cv;
using namespace Seamline;

static bool endsWith(const string& s, const string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Aperçu réduit de la mosaïque avec les RECOUVREMENTS en surbrillance (bleu : entre tuiles côte à côte, vert : l'une
// au-dessus de l'autre), les cadres des tuiles et les coutures en rouge. Affichage seulement.
static Mat drawOverlaps(const Mat& mosaic, const Mat& index_matrix, const vector<Size>& sizes, const vector<Point>& corners,
                        int maxSide = 1400) {
    const double k = min(1.0, (double)maxSide / max(mosaic.cols, mosaic.rows));
    Mat img, idx;
    resize(mosaic, img, Size(), k, k, INTER_AREA);
    resize(index_matrix, idx, img.size(), 0, 0, INTER_NEAREST);
    auto scaled = [&](const Rect& r) {
        return Rect(cvRound(r.x * k), cvRound(r.y * k), max(1, cvRound(r.width * k)), max(1, cvRound(r.height * k))) &
               Rect(0, 0, img.cols, img.rows);
    };
    vector<Rect> rects;
    for (size_t i = 0; i < sizes.size(); ++i) rects.push_back(Rect(corners[i], sizes[i]));
    for (size_t i = 0; i < rects.size(); ++i) {
        for (size_t j = i + 1; j < rects.size(); ++j) {
            const Rect o = rects[i] & rects[j];
            if (o.area() <= 0) continue;
            const Scalar tint = o.width < o.height ? Scalar(255, 120, 0) : Scalar(0, 200, 60);     // H : bleu, V : vert (BGR)
            Mat roi = img(scaled(o));
            roi = roi * 0.65 + Mat(roi.size(), roi.type(), tint) * 0.35;
        }
    }
    for (size_t i = 0; i < rects.size(); ++i) {
        rectangle(img, scaled(rects[i]), Scalar(0, 255, 255), 1);
        putText(img, to_string(i), Point(cvRound(rects[i].x * k) + 6, cvRound(rects[i].y * k) + 18), FONT_HERSHEY_SIMPLEX, 0.5,
                Scalar(0, 255, 255), 1, LINE_AA);
    }
    for (int y = 1; y < idx.rows; ++y)
        for (int x = 1; x < idx.cols; ++x) {
            const uchar l = idx.at<uchar>(y, x);
            if (l != idx.at<uchar>(y, x - 1) || l != idx.at<uchar>(y - 1, x)) img.at<Vec3b>(y, x) = Vec3b(0, 0, 255);
        }
    putText(img, "bleu : recouvrement H | vert : recouvrement V | rouge : couture | jaune : tuiles", Point(10, img.rows - 12),
            FONT_HERSHEY_SIMPLEX, 0.55, Scalar(255, 255, 255), 2, LINE_AA);
    return img;
}

int main(int argc, char* argv[]) {
    // options facultatives :
    //   --qualite=<facteur>       (0 < facteur <= 1, defaut 1) : reduction fixe des copies utilisees pour la coupe de graphe
    //   --chevauchement=<fraction> (0 < fraction <= 1, defaut 0.3) : bande de la tuile examinee par SIFT pour le positionnement
    double scale = 1;
    double overlap = 0.3;
    vector<string> args;
    for (int i = 1; i < argc; ++i) {
        const string a = argv[i];
        if (a.rfind("--qualite=", 0) == 0) {
            try { scale = stod(a.substr(10)); } catch (...) { scale = -1; }
        } else if (a.rfind("--chevauchement=", 0) == 0) {
            try { overlap = stod(a.substr(16)); } catch (...) { overlap = -1; }
        } else args.push_back(a);
    }

    if (args.size() < 1 || !(scale > 0.0 && scale <= 1.0) || !(overlap > 0.0 && overlap <= 1.0)) {
        cout << "Usage: " << argv[0] << " [--qualite=1] [--chevauchement=0.3] <tuile1_rLL_cCC.tif> [tuile2...]" << endl;
        cout << "  --qualite=F         : facteur de reduction fixe (0 < F <= 1, 1 = pleine resolution) des copies pour la coupe de graphe (defaut 1)" << endl;
        cout << "  --chevauchement=F   : fraction de la tuile examinee par SIFT pour calculer les positions (0 < F <= 1, defaut 0.3)" << endl;
        return -1;
    }

    const vector<string> tile_paths(args.begin(), args.end());

    // --- CONSTRUCTION DE LA STRUCTURE : positions calculees par SIFT (16 bits) puis RANSAC, ajustement global ---
    vector<TilePositionData> input_positions;
    stitching::positioning::PositioningSettings positioning;
    positioning.overlap = (float)overlap;
    cout << "Calcul des positions (SIFT + RANSAC, 16 bits, chevauchement " << overlap << ")..." << endl;
    if (!stitching::positioning::computeTilePositions(tile_paths, positioning, input_positions, cout)) {
        cout << "Echec du calcul des positions" << endl;
        return -1;
    }
    if (input_positions.size() != tile_paths.size()) {
        cout << "Echec : " << input_positions.size() << " tuile(s) positionnee(s) sur " << tile_paths.size() << endl;
        return -1;
    }
    for (const auto& p : input_positions)
        cout << "  " << p.tile_id << " : (" << p.position.x << ", " << p.position.y << ")" << endl;
    // ------------------------------------

    vector<Size> tile_sizes;
    vector<UMat> masks_umat_small;
    vector<Point> original_corners, corners_small;

    ImagePretreatment pretreatment;
    cout << "Lecture des tailles (16 bits, une tuile a la fois), qualite x" << scale << "..." << endl;
    
    if (pretreatment.processImages(input_positions, tile_paths, scale, tile_sizes, masks_umat_small,
                                   original_corners, corners_small) < 0) return -1;

    Mat final_mosaic, index_matrix;
    GraphCut graphcut;
    cout << "Calcul GraphCut (COST_COLOR d'OpenCV, 2 tuiles a la fois)..." << endl;
    if (graphcut.computeSeamline(tile_paths, tile_sizes, original_corners, scale, corners_small, masks_umat_small,
                                 final_mosaic, index_matrix) < 0) {
        cout << "Echec du calcul" << endl;
        return -1;
    }

    cout << "Extraction des territoires..." << endl;
    vector<TileTerritory> territories = graphcut.extractPolygons(index_matrix, (int)tile_sizes.size());
    for (const auto& territory : territories) {
        size_t holes = 0;
        for (const auto& h : territory.holes) holes += h.size();
        cout << "Tuile " << territory.tile_index << " : " << territory.polygons.size() << " polygone(s), " << holes << " trou(s)." << endl;
    }

    SeamCheck check = graphcut.verifyTiling(territories, index_matrix, tile_sizes, original_corners);
    cout << "Pavage exact : " << (check.exactTiling ? "oui" : "NON") << " | pixels differents " << check.differentPixels
         << " | chevauchements " << check.overlapPixels << " px | trous " << check.gapPixels << " px"
         << " | pixels reattribues (arrondis de la grille reduite) " << check.fallbackPixels << endl;

    imwrite("final_mosaic.jpg", final_mosaic);
    imwrite("overlaps_mosaic.jpg", drawOverlaps(final_mosaic, index_matrix, tile_sizes, original_corners));
    Mat index_visual;
    normalize(index_matrix, index_visual, 0, 255, NORM_MINMAX, CV_8U, index_matrix != NO_TILE);
    imwrite("index_matrix.png", index_visual);

    cout << "Termine. Fichiers : final_mosaic.jpg, overlaps_mosaic.jpg, index_matrix.png" << endl;
    return check.exactTiling && check.gapPixels == 0 && check.overlapPixels == 0 ? 0 : 1;
}