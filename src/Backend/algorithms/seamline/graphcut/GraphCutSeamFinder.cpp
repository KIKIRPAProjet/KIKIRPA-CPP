#include "algorithms/seamline/graphcut/GraphCutSeamFinder.h"
#include <string>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#if __has_include(<opencv2/geometry/2d.hpp>)
#include <opencv2/geometry/2d.hpp>
#endif
#include <opencv2/stitching/detail/seam_finders.hpp>
#include <opencv2/stitching/detail/util.hpp>
#include "algorithms/seamline/graphcut/ImagePretreatment.h"
#include <climits>
#include <vector>
using namespace std;
using namespace cv;
using namespace cv::detail;
namespace Seamline {

// Relit une tuile et la convertit en UMat flottant 0–255 (exigée par le GraphCutSeamFinder d'OpenCV).
// Renvoie une UMat vide si la lecture échoue ou si la taille ne correspond pas au masque réduit attendu.
static UMat loadSmallUMat(const string& path, double scale, const Size& expected) {
    Mat f = ImagePretreatment::readSmallFloat(path, scale);
    UMat u;
    if (f.empty() || f.size() != expected) return u;
    f.copyTo(u);
    return u;
}

int GraphCut::computeSeamline(const vector<string>& tile_paths, const vector<Size>& tile_sizes, const vector<Point>& original_corners, double scale, vector<Point>& corners_small, vector<UMat>& masks_umat_small, Mat& final_mosaic, Mat& index_matrix) {
    const int n = (int)tile_sizes.size();
    if (n == 0 || n >= (int)NO_TILE) return -1;          // la carte de provenance est sur 8 bits (255 = aucune tuile)
    if ((int)tile_paths.size() != n || (int)masks_umat_small.size() != n ||
        (int)original_corners.size() != n || (int)corners_small.size() != n) return -1;

    // Application de l'algorithme GraphCut d'OpenCV (COST_COLOR) PAIRE PAR PAIRE, dans l'ordre (i, j>i) que le seam finder
    // utilise en interne. Les masques (8 bits) sont partagés avec la liste globale : la modification faite sur la paire
    // (i, j) est donc vue par les paires suivantes, comme dans l'appel unique d'origine. Les tuiles sont relues depuis le
    // disque : au plus DEUX en mémoire à un instant donné (i, puis chaque j qui la chevauche).
    Ptr<SeamFinder> seam_finder = makePtr<GraphCutSeamFinder>(GraphCutSeamFinderBase::COST_COLOR);
    for (int i = 0; i < n - 1; ++i) {
        UMat img_i;                                      // chargée à la première paire qui l'utilise, libérée en fin de tour
        for (int j = i + 1; j < n; ++j) {
            Rect roi;
            if (!overlapRoi(corners_small[i], corners_small[j], masks_umat_small[i].size(), masks_umat_small[j].size(), roi)) continue;
            if (img_i.empty()) {
                img_i = loadSmallUMat(tile_paths[i], scale, masks_umat_small[i].size());
                if (img_i.empty()) { cout << "Impossible to read : " << tile_paths[i] << endl; return -1; }
            }
            UMat img_j = loadSmallUMat(tile_paths[j], scale, masks_umat_small[j].size());
            if (img_j.empty()) { cout << "Impossible to read : " << tile_paths[j] << endl; return -1; }
            vector<UMat> pair_images{img_i, img_j};
            vector<Point> pair_corners{corners_small[i], corners_small[j]};
            vector<UMat> pair_masks{masks_umat_small[i], masks_umat_small[j]};
            seam_finder->find(pair_images, pair_corners, pair_masks);
        }
    }

    // ---- taille de la toile (pleine résolution) et rectangles EXACTS des tuiles
    int max_x = 0, max_y = 0;
    vector<Rect> rects(n);
    for (int i = 0; i < n; ++i) {
        rects[i] = Rect(original_corners[i], tile_sizes[i]);
        max_x = max(max_x, rects[i].x + rects[i].width);
        max_y = max(max_y, rects[i].y + rects[i].height);
    }

    // ---- carte de provenance sur la grille RÉDUITE : quelle tuile a gardé chaque pixel après la coupe
    int ws = 1, hs = 1;
    for (int i = 0; i < n; ++i) {
        Size s = masks_umat_small[i].size();
        ws = max(ws, corners_small[i].x + s.width);
        hs = max(hs, corners_small[i].y + s.height);
    }
    Mat small_label(hs, ws, CV_8U, Scalar(NO_TILE));
    for (int i = 0; i < n; ++i) {
        Mat m = masks_umat_small[i].getMat(ACCESS_READ);
        for (int y = 0; y < m.rows; ++y) {
            const uchar* pm = m.ptr<uchar>(y);
            uchar* pl = small_label.ptr<uchar>(corners_small[i].y + y) + corners_small[i].x;
            for (int x = 0; x < m.cols; ++x)
                if (pm[x] > 127 && pl[x] == NO_TILE) pl[x] = (uchar)i;      // même seuil que la version d'origine
        }
    }
    // pixels réduits qu'aucune tuile n'a gardés (arrondis aux bords) : prennent la tuile du pixel gardé le plus proche
    {
        Mat unclaimed = (small_label == NO_TILE), dist, lab;
        if (countNonZero(unclaimed) > 0 && countNonZero(unclaimed) < (int)unclaimed.total()) {
            distanceTransform(unclaimed, dist, lab, DIST_L2, DIST_MASK_5, DIST_LABEL_PIXEL);
            double lmax;
            minMaxLoc(lab, nullptr, &lmax);
            vector<uchar> table((size_t)lmax + 1, NO_TILE);
            for (int y = 0; y < hs; ++y)
                for (int x = 0; x < ws; ++x)
                    if (!unclaimed.at<uchar>(y, x)) table[lab.at<int>(y, x)] = small_label.at<uchar>(y, x);
            for (int y = 0; y < hs; ++y)
                for (int x = 0; x < ws; ++x)
                    if (unclaimed.at<uchar>(y, x)) small_label.at<uchar>(y, x) = table[lab.at<int>(y, x)];
        }
    }

    // ---- facteur de réduction réellement appliqué (même rapport sur le grand côté)
    const Size s0 = masks_umat_small[0].size();
    const double grid_scale = (double)max(s0.width, s0.height) / (double)max(tile_sizes[0].width, tile_sizes[0].height);
    vector<int> xs(max_x), ys(max_y);
    for (int x = 0; x < max_x; ++x) xs[x] = min(ws - 1, (int)(x * grid_scale));
    for (int y = 0; y < max_y; ++y) ys[y] = min(hs - 1, (int)(y * grid_scale));

    // ---- carte de provenance EXACTE en pleine résolution : un seul numéro par pixel, parmi les tuiles qui le couvrent
    index_matrix = Mat(Size(max_x, max_y), CV_8U, Scalar(NO_TILE));
    for (int i = 0; i < n; ++i) {                        // 1) la tuile désignée par la coupe, si elle couvre le pixel
        for (int y = 0; y < rects[i].height; ++y) {
            uchar* pi = index_matrix.ptr<uchar>(rects[i].y + y) + rects[i].x;
            const uchar* sl = small_label.ptr<uchar>(ys[rects[i].y + y]);
            for (int x = 0; x < rects[i].width; ++x)
                if (sl[xs[rects[i].x + x]] == i) pi[x] = (uchar)i;
        }
    }
    vector<Point2f> centers(n);
    for (int i = 0; i < n; ++i) centers[i] = Point2f(rects[i].x + rects[i].width / 2.f, rects[i].y + rects[i].height / 2.f);
    fallbackPixels = 0;
    for (int y = 0; y < max_y; ++y) {                    // 2) pixel couvert mais non attribué (arrondis de la grille réduite)
        vector<int> row_tiles;
        for (int i = 0; i < n; ++i) if (y >= rects[i].y && y < rects[i].y + rects[i].height) row_tiles.push_back(i);
        uchar* pi = index_matrix.ptr<uchar>(y);
        const uchar* sl = small_label.ptr<uchar>(ys[y]);
        for (int x = 0; x < max_x; ++x) {
            if (pi[x] != NO_TILE) continue;
            const int wanted = sl[xs[x]];                // tuile désignée par la coupe (qui ne couvre pas ce pixel)
            int best = -1;
            float best_d = 1e30f;
            for (int i : row_tiles) {
                if (x < rects[i].x || x >= rects[i].x + rects[i].width) continue;
                const float d = wanted >= n ? 0.f : (float)norm(centers[i] - centers[wanted]);
                if (d < best_d) { best_d = d; best = i; }      // la plus proche de la tuile désignée
            }
            if (best >= 0) { pi[x] = (uchar)best; ++fallbackPixels; }
        }
    }

    // ---- mosaïque visuelle (8 bits, affichage seulement) : les tuiles pleine résolution sont relues UNE À LA FOIS
    final_mosaic = Mat::zeros(Size(max_x, max_y), CV_8UC3);
    for (int i = 0; i < n; ++i) {
        Mat tile16 = ImagePretreatment::read16(tile_paths[i]);
        if (tile16.empty() || tile16.size() != tile_sizes[i]) return -1;
        Mat tile8;
        tile16.convertTo(tile8, CV_8UC3, 255.0 / 65535.0);
        tile16.release();
        Mat mask = (index_matrix(rects[i]) == i);
        tile8.copyTo(final_mosaic(rects[i]), mask);
    }   // <- une seule tuile pleine résolution en mémoire à la fois
    return 1;
}

vector<TileTerritory> GraphCut::extractPolygons(const Mat &index_matrix, int num_tiles) {
    vector<TileTerritory> territories;
    for (int i = 0; i < num_tiles; ++i) {
        // Création d'un masque binaire strictement dédié à la tuile 'i'
        Mat mask_tile = (index_matrix == i);

        // Contours extérieurs ET trous (RETR_CCOMP) : sans les trous, le pavage ne serait pas exact.
        vector<vector<Point>> contours;
        vector<Vec4i> hier;
        findContours(mask_tile, contours, hier, RETR_CCOMP, CHAIN_APPROX_SIMPLE);

        TileTerritory territory;
        territory.tile_index = i;
        for (size_t j = 0; j < contours.size(); ++j) {
            if (hier[j][3] >= 0) continue;               // un trou : rattaché à son polygone ci-dessous
            vector<vector<Point>> holes;
            for (int k = hier[j][2]; k >= 0; k = hier[k][0]) holes.push_back(contours[k]);
            // aucun filtre d'aire : un contour dégénéré (1 pixel de large) fait partie du territoire
            territory.polygons.push_back(contours[j]);
            territory.holes.push_back(holes);
        }
        territories.push_back(territory);
    }
    return territories;
}

SeamCheck GraphCut::verifyTiling(const vector<TileTerritory>& territories, const Mat& index_matrix,
                                 const vector<Size>& tile_sizes, const vector<Point>& original_corners) const {
    SeamCheck c;
    c.fallbackPixels = fallbackPixels;
    Mat recomposed(index_matrix.size(), CV_8U, Scalar(NO_TILE));
    const Rect canvas(0, 0, index_matrix.cols, index_matrix.rows);
    for (const TileTerritory& t : territories) {
        if (t.polygons.empty()) continue;
        vector<Point> all;
        for (const auto& p : t.polygons) all.insert(all.end(), p.begin(), p.end());
        const Rect bb = boundingRect(all) & canvas;
        if (bb.area() <= 0) continue;
        Mat m = Mat::zeros(bb.size(), CV_8U);
        const Point off(-bb.x, -bb.y);
        for (size_t i = 0; i < t.polygons.size(); ++i) {
            drawContours(m, vector<vector<Point>>{t.polygons[i]}, -1, Scalar(255), FILLED, LINE_8, noArray(), INT_MAX, off);
            for (const auto& hole : t.holes[i]) {
                const vector<vector<Point>> h{hole};
                drawContours(m, h, -1, Scalar(0), FILLED, LINE_8, noArray(), INT_MAX, off);     // le trou est vidé…
                drawContours(m, h, -1, Scalar(255), 1, LINE_8, noArray(), INT_MAX, off);        // …mais son contour appartient au territoire
                ++c.holes;
                c.vertices += (long)hole.size();
            }
            c.vertices += (long)t.polygons[i].size();
        }
        c.polygons += (int)t.polygons.size();
        Mat rc = recomposed(bb), filled;
        compare(rc, Scalar(NO_TILE), filled, CMP_NE);
        c.overlapPixels += countNonZero(filled & m);
        rc.setTo(t.tile_index, m);
    }
    Mat diff;
    compare(recomposed, index_matrix, diff, CMP_NE);
    c.differentPixels = countNonZero(diff);
    c.exactTiling = c.differentPixels == 0;

    Mat covered = Mat::zeros(index_matrix.size(), CV_8U);
    for (size_t i = 0; i < tile_sizes.size(); ++i)
        covered(Rect(original_corners[i], tile_sizes[i])).setTo(255);
    c.gapPixels = countNonZero((index_matrix == NO_TILE) & covered);
    return c;
}

}  // namespace Seamline