#include "algorithms/seamline/GraphCutSeamFinder.h"
#include <string>
#include <regex>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/stitching/detail/seam_finders.hpp>
#include <vector>
using namespace std;
using namespace cv;
using namespace cv::detail;
namespace Seamline {

int GraphCut::computeSeamline(const vector<Mat> &original_images, const vector<Point> &original_corners, vector<UMat> &images_umat_small,vector<Point> &corners_small, vector<UMat> &masks_umat_small, const Size &common_size, Mat &final_mosaic, Mat &index_matrix) {
    // calcul de la taille finale de la toile
    Ptr<SeamFinder> seam_finder = makePtr<GraphCutSeamFinder>(GraphCutSeamFinderBase::COST_COLOR); // Création d'un objet SeamFinder pour trouver les coutures entre les tuiles en utilisant l'algorithme GraphCut avec un coût basé sur la couleur (COST_COLOR), COST_COLOR_GRAD n'est pas efficace apparemment
    seam_finder->find(images_umat_small, corners_small, masks_umat_small); // Application de l'algorithme GraphCut pour trouver les coutures entre les tuiles en utilisant les images réduites, les coins réduits et les masques réduits
    int max_x = 0, max_y = 0;
    for (size_t i = 0; i < original_images.size(); ++i) {
        max_x = max(max_x, original_corners[i].x + original_images[i].cols);
        max_y = max(max_y, original_corners[i].y + original_images[i].rows);
    }
    
    // Initialisation des DEUX matrices
    final_mosaic = Mat::zeros(Size(max_x, max_y), CV_8UC3); // Image visuelle
    index_matrix = Mat(Size(max_x, max_y), CV_8U, Scalar(255));   // Matrice de données (fond à 255) (possiblement pas utile, à vérifier par la suite)
    
    for (size_t i = 0; i < masks_umat_small.size(); ++i) {
        Mat mask_optimized_small = masks_umat_small[i].getMat(ACCESS_READ);
        Mat mask_optimized;
        
        // Agrandissement du masque avec INTER_NEAREST pour garder des bords stricts
        resize(mask_optimized_small, mask_optimized, common_size, 0, 0, INTER_NEAREST);
        threshold(mask_optimized, mask_optimized, 127, 255, THRESH_BINARY);

        // On dilate (gonfle) le masque pour boucher les fissures
        Mat kernel = getStructuringElement(MORPH_RECT, Size(3, 3));
        dilate(mask_optimized, mask_optimized, kernel);

        Rect roi(original_corners[i].x, original_corners[i].y, common_size.width, common_size.height); // Définition de la région d'intérêt (ROI) dans l'image finale pour placer la tuile actuelle
        
        // 1. On copie l'image originale pour le visuel
        original_images[i].copyTo(final_mosaic(roi), mask_optimized);
        
        // 2. On copie l'index pour la matrice de données
        Mat index_roi(common_size, CV_8U, Scalar(i));
        index_roi.copyTo(index_matrix(roi), mask_optimized);
    }
    return 1;
}
std::vector<TileTerritory> GraphCut::extractPolygons(const Mat &index_matrix, int num_tiles) {
    std::vector<TileTerritory> territories;

    for (int i = 0; i < num_tiles; ++i) {
        // Création d'un masque binaire strictement dédié à la tuile 'i'
        Mat mask_tile = (index_matrix == i);
        
        // Extraction des contours délimitant les pixels de cette tuile (RETR_EXTERNAL pour l'enveloppe)
        vector<vector<Point>> contours;
        findContours(mask_tile, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

        TileTerritory territory;
        territory.tile_index = i;

        for (size_t j = 0; j < contours.size(); ++j) {
            // Filtrer les éventuels artefacts résiduels
            if (contourArea(contours[j]) > 0) {
                territory.polygons.push_back(contours[j]);
            }
        }
        
        territories.push_back(territory);
    }

    return territories;
}
};
