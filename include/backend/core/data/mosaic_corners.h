// include/backend/core/data/mosaic_corners.h
#pragma once

#include <opencv2/core/types.hpp>

#include <vector>

namespace DataNs {

    // Coin haut-gauche de chaque tuile dans le repère de la mosaïque, dans l'ordre de `positions` (positions de
    // absolute-pos, en pixels, éventuellement négatives et fractionnaires). Le minimum des positions est soustrait, puis
    // chaque coin est arrondi au pixel (cvRound) : la tuile la plus à gauche est en x = 0, la plus haute en y = 0.
    // Seule définition de ce repère : graphcut y calcule les territoires, fusion y place les pixels. Vide si
    // `positions` est vide.
    std::vector<cv::Point> computeMosaicCorners(const std::vector<cv::Point2d>& positions);

}
