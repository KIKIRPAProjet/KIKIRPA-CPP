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

// Charge une tuile réduite en flottants sur l'échelle [0, 255], puis la copie dans une UMat utilisée par le calcul des raccords
// Renvoie une UMat vide si la lecture échoue ou si les dimensions ne correspondent pas à celles du masque attendu
static UMat loadSmallUMat(const string& path, double scale, const Size& expected) {
    Mat f = ImagePretreatment::readSmallFloat(path, scale); 
    UMat u;
    if (f.empty() || f.size() != expected) return u; 
    f.copyTo(u); 
    return u;
}

// Calcule les raccords sur les images réduites, puis attribue chaque pixel pleine résolution à une tuile qui le couvre
//
// Résultats :
// - masks_umat_small : masques modifiés par le calcul des raccords
// - index_matrix : numéro de la tuile source pour chaque pixel
// - final_mosaic : mosaïque de visualisation en couleur sur 8 bits
//
// Les positions doivent être exprimées dans un repère à coordonnées positives ou nulles, comme celui préparé par ImagePretreatment
// Renvoie 1 en cas de succès, ou -1 lorsqu'une erreur est détectée
// Une erreur peut laisser les masques et les sorties partiellement modifiés
int GraphCut::computeSeamline(const vector<string>& tile_paths, const vector<Size>& tile_sizes, const vector<Point>& original_corners, double scale, vector<Point>& corners_small, vector<UMat>& masks_umat_small, Mat& final_mosaic, Mat& index_matrix) {
    // 1. Vérification du nombre de tuiles et de la cohérence des listes 
    // La carte de provenance utilise un octet par pixel
    // La valeur NO_TILE (255) est réservée aux pixels sans tuile
    const int n = (int)tile_sizes.size(); 
    if (n == 0 || n >= (int)NO_TILE) return -1;       
    if ((int)tile_paths.size() != n || (int)masks_umat_small.size() != n ||
        (int)original_corners.size() != n || (int)corners_small.size() != n) return -1;

    // 2. Calcul des raccords entre les paires de tuiles qui se chevauchent
    // Le coût fondé sur la couleur cherche un raccord visuellement discret
    // Les masques des paires partagent leurs données avec les masques globaux : les modifications d'une paire sont donc visibles par les suivantes
    //
    // Les images réduites sont chargées à la demande : au plus deux tuiles sources sont conservées simultanément, en plus de la mémoire de calcul
    Ptr<SeamFinder> seam_finder = makePtr<GraphCutSeamFinder>(GraphCutSeamFinderBase::COST_COLOR); 
    
    for (int i = 0; i < n - 1; ++i) {
        UMat img_i;                   
        for (int j = i + 1; j < n; ++j) {
            Rect roi;
            
            if (!overlapRoi(corners_small[i], corners_small[j], masks_umat_small[i].size(), masks_umat_small[j].size(), roi)) continue;
            // Charger la tuile i seulement à son premier chevauchement.
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

    // 3. Détermination des rectangles des tuiles et de la taille de la mosaïque
    // Les bords droits et inférieurs les plus éloignés fixent les dimensions du canevas pleine résolution
    int max_x = 0, max_y = 0;
    vector<Rect> rects(n);
    for (int i = 0; i < n; ++i) {
        rects[i] = Rect(original_corners[i], tile_sizes[i]); 
        max_x = max(max_x, rects[i].x + rects[i].width);
        max_y = max(max_y, rects[i].y + rects[i].height);
    }

    // 4. Construction de la carte de provenance sur la grille réduite 
    // Chaque valeur indique un numéro de tuile, et non une couleur
    // Si plusieurs masques conservent le même pixel, la première tuile gagne
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
                if (pm[x] > 127 && pl[x] == NO_TILE) pl[x] = (uchar)i;      
        }
    }

    // 5. Compléter les zones non attribuées de la carte réduite 
    // Propager le numéro d'un pixel attribué voisin à l'aide d'une transformation de distance, s'il existe des pixels attribués
    // Cette propagation peut dépasser la couverture réelle des tuiles : celle-ci sera vérifiée lors du passage en pleine résolution
    {
        Mat unclaimed = (small_label == NO_TILE), dist, lab;
        if (countNonZero(unclaimed) > 0 && countNonZero(unclaimed) < (int)unclaimed.total()) {
            distanceTransform(unclaimed, dist, lab, DIST_L2, DIST_MASK_5, DIST_LABEL_PIXEL); 
            double lmax;
            minMaxLoc(lab, nullptr, &lmax);
            vector<uchar> table((size_t)lmax + 1, NO_TILE);
            // Associer les étiquettes de distance aux numéros de tuiles
            for (int y = 0; y < hs; ++y)
                for (int x = 0; x < ws; ++x)
                    if (!unclaimed.at<uchar>(y, x)) table[lab.at<int>(y, x)] = small_label.at<uchar>(y, x);
            for (int y = 0; y < hs; ++y)
                for (int x = 0; x < ws; ++x)
                    if (unclaimed.at<uchar>(y, x)) small_label.at<uchar>(y, x) = table[lab.at<int>(y, x)];
        }
    }

    // 6. Préparer la correspondance entre les deux résolutions
    // Estimer le facteur réellement appliqué à partir du grand côté de la première tuile, afin de tenir compte des arrondis de taille
    const Size s0 = masks_umat_small[0].size();
    const double grid_scale = (double)max(s0.width, s0.height) / (double)max(tile_sizes[0].width, tile_sizes[0].height);
    vector<int> xs(max_x), ys(max_y);
    
    for (int x = 0; x < max_x; ++x) xs[x] = min(ws - 1, (int)(x * grid_scale));
    for (int y = 0; y < max_y; ++y) ys[y] = min(hs - 1, (int)(y * grid_scale));

    // 7. Attribuer les pixels pleine résolution selon la carte réduite
    // Une attribution n'est acceptée que si la tuile désignée couvre réellement le pixel. Le détail du raccord reste issu de la grille réduite.
    index_matrix = Mat(Size(max_x, max_y), CV_8U, Scalar(NO_TILE));
    for (int i = 0; i < n; ++i) {                        
        for (int y = 0; y < rects[i].height; ++y) {
            uchar* pi = index_matrix.ptr<uchar>(rects[i].y + y) + rects[i].x;
            const uchar* sl = small_label.ptr<uchar>(ys[rects[i].y + y]);
            for (int x = 0; x < rects[i].width; ++x)
                if (sl[xs[rects[i].x + x]] == i) pi[x] = (uchar)i;
        }
    }
    // 8. Réparer les pixels couverts mais encore non attribués 
    // Parmi les tuiles couvrant le pixel, choisir celle dont le centre est le plus proche du centre de la tuile désignée par la carte réduite
    // Sans tuile désignée valide, choisir la première tuile couvrante
    // Les pixels hors de toute tuile restent à NO_TILE
    vector<Point2f> centers(n);
    for (int i = 0; i < n; ++i) centers[i] = Point2f(rects[i].x + rects[i].width / 2.f, rects[i].y + rects[i].height / 2.f);
    fallbackPixels = 0;
    for (int y = 0; y < max_y; ++y) {                    
        vector<int> row_tiles;
        for (int i = 0; i < n; ++i) if (y >= rects[i].y && y < rects[i].y + rects[i].height) row_tiles.push_back(i);
        uchar* pi = index_matrix.ptr<uchar>(y);
        const uchar* sl = small_label.ptr<uchar>(ys[y]);
        for (int x = 0; x < max_x; ++x) {
            if (pi[x] != NO_TILE) continue;
            const int wanted = sl[xs[x]];                
            int best = -1;
            float best_d = 1e30f;
            for (int i : row_tiles) {
                if (x < rects[i].x || x >= rects[i].x + rects[i].width) continue;
                const float d = wanted >= n ? 0.f : (float)norm(centers[i] - centers[wanted]); 
                if (d < best_d) { best_d = d; best = i; }      
            }
            if (best >= 0) { pi[x] = (uchar)best; ++fallbackPixels; } 
        }
    }

    // 9. Construire la mosaïque de visualisation sur 8 bits 
    // Relire les tuiles pleine résolution une par une et copier uniquement leurs pixels attribués. Aucun mélange entre les tuiles n'est effectué
    // Les zones sans tuile restent noires
    final_mosaic = Mat::zeros(Size(max_x, max_y), CV_8UC3); 
    for (int i = 0; i < n; ++i) {
        Mat tile16 = ImagePretreatment::read16(tile_paths[i]);
        if (tile16.empty() || tile16.size() != tile_sizes[i]) return -1;
        Mat tile8;
        tile16.convertTo(tile8, CV_8UC3, 255.0 / 65535.0);
        tile16.release();
        Mat mask = (index_matrix(rects[i]) == i); 
        tile8.copyTo(final_mosaic(rects[i]), mask); 
    }   
    return 1;
}
// Convertit la carte de provenance en territoires polygonaux, un par tuile
// Un territoire peut contenir plusieurs régions séparées et des trous
// Les contours restent exprimés dans les coordonnées de la mosaïque
vector<TileTerritory> GraphCut::extractPolygons(const Mat &index_matrix, int num_tiles) { 
    vector<TileTerritory> territories;
    for (int i = 0; i < num_tiles; ++i) {
        // Isoler les pixels appartenant à la tuile
        Mat mask_tile = (index_matrix == i); 

        //Extraire les contours extérieurs et leurs trous
        // RETR_CCOMP fournit une hiérarchie permettant de rattacher chaque trou à son contour extérieur
        vector<vector<Point>> contours;
        vector<Vec4i> hier;
        findContours(mask_tile, contours, hier, RETR_CCOMP, CHAIN_APPROX_SIMPLE); 
        //Regrouper chaque contour extérieur avec ses trous
        TileTerritory territory;
        territory.tileIndex = i;
        for (size_t j = 0; j < contours.size(); ++j) {
            if (hier[j][3] >= 0) continue;               
            vector<vector<Point>> holes;
            for (int k = hier[j][2]; k >= 0; k = hier[k][0]) holes.push_back(contours[k]);
            // Ne pas filtrer les petites régions ou les contours dégénérés : même une région d'un pixel de large appartient au territoire
            territory.polygons.push_back(contours[j]);
            territory.holes.push_back(holes);
        }
        territories.push_back(territory);
    }
    return territories;
}

// Redessine les territoires et compare le résultat à la carte de provenance
// Compte également les chevauchements, les pixels couverts sans attribution, les contours, les trous et leurs sommets exactTiling indique uniquement 
//l'égalité entre la reconstruction et index_matrix
//Les chevauchements et les lacunes sont comptés séparément
SeamCheck GraphCut::verifyTiling(const vector<TileTerritory>& territories, const Mat& index_matrix,
                                 const vector<Size>& tile_sizes, const vector<Point>& original_corners) const {
    SeamCheck c;
    c.fallbackPixels = fallbackPixels;
    Mat recomposed(index_matrix.size(), CV_8U, Scalar(NO_TILE)); 
    const Rect canvas(0, 0, index_matrix.cols, index_matrix.rows);
    // Reconstruire chaque territoire à partir de ses contours
    for (const TileTerritory& t : territories) {
        if (t.polygons.empty()) continue;
        // Limiter le masque temporaire au rectangle englobant le territoire
        vector<Point> all;
        for (const auto& p : t.polygons) all.insert(all.end(), p.begin(), p.end());
        const Rect bb = boundingRect(all) & canvas;
        if (bb.area() <= 0) continue;
        Mat m = Mat::zeros(bb.size(), CV_8U);
        const Point off(-bb.x, -bb.y);
        for (size_t i = 0; i < t.polygons.size(); ++i) {
            drawContours(m, vector<vector<Point>>{t.polygons[i]}, -1, Scalar(255), FILLED, LINE_8, noArray(), INT_MAX, off);
            // Vider l'intérieur des trous, puis rétablir leur contour : ces pixels de bord appartiennent au territoire extérieur
            for (const auto& hole : t.holes[i]) {
                const vector<vector<Point>> h{hole};
                drawContours(m, h, -1, Scalar(0), FILLED, LINE_8, noArray(), INT_MAX, off);     
                drawContours(m, h, -1, Scalar(255), 1, LINE_8, noArray(), INT_MAX, off);       
                ++c.holes;
                c.vertices += (long)hole.size();
            }
            c.vertices += (long)t.polygons[i].size();
        }
        c.polygons += (int)t.polygons.size();
        // Compter les pixels déjà occupés avant d'inscrire le territoire
        Mat rc = recomposed(bb), filled;
        compare(rc, Scalar(NO_TILE), filled, CMP_NE);
        c.overlapPixels += countNonZero(filled & m);
        rc.setTo(t.tileIndex, m);
    }
    //Comparer la reconstruction à la carte de provenance
    Mat diff;
    compare(recomposed, index_matrix, diff, CMP_NE);
    c.differentPixels = countNonZero(diff);
    c.exactTiling = c.differentPixels == 0; 
    // Détecter les pixels couverts par une tuile mais non attribués
    // Les zones extérieures à toutes les tuiles ne sont pas des lacunes
    Mat covered = Mat::zeros(index_matrix.size(), CV_8U);
    for (size_t i = 0; i < tile_sizes.size(); ++i)
        covered(Rect(original_corners[i], tile_sizes[i])).setTo(255);
    c.gapPixels = countNonZero((index_matrix == NO_TILE) & covered);
    return c;
}

}  // namespace Seamline
