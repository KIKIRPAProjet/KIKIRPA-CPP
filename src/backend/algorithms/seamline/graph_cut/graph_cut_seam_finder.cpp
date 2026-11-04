// src/backend/algorithms/seamline/graph_cut/graph_cut_seam_finder.cpp
#include "algorithms/seamline/graph_cut/graph_cut_seam_finder.h"

#include "algorithms/seamline/graph_cut/image_pretreatment.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#if __has_include(<opencv2/geometry/2d.hpp>)
#include <opencv2/geometry/2d.hpp>
#endif
#include <opencv2/stitching/detail/seam_finders.hpp>
#include <opencv2/stitching/detail/util.hpp>

#include <algorithm>
#include <climits>
#include <iostream>
#include <string>
#include <vector>

namespace GraphCutNs {

    namespace {

        // Relit une tuile et la convertit en UMat flottant 0–255 (exigée par le GraphCutSeamFinder d'OpenCV).
        // Renvoie une UMat vide si la lecture échoue ou si la taille ne correspond pas au masque réduit attendu.
        cv::UMat loadSmallUMat(const std::string& path, double scale, const cv::Size& expectedSize) {
            const cv::Mat floatImage = ImagePretreatmentClass::readSmallFloat(path, scale);
            cv::UMat smallImage;
            // Lecture impossible ou dimensions différentes du masque : image vide
            if (floatImage.empty() || floatImage.size() != expectedSize) return smallImage;
            floatImage.copyTo(smallImage);   // conteneur d'image OpenCV attendu par le seam finder
            return smallImage;
        }

    }

    // Sorties : smallMasks -> masques mis à jour (parties de chaque tuile réduite à garder) ;
    // tileIndexMap -> carte pleine résolution de la tuile source de chaque pixel ;
    // finalMosaic -> image couleur 8 bits assemblée à partir de cette carte.
    int GraphCutSeamFinderClass::computeSeamline(const std::vector<std::string>& tilePaths, const std::vector<cv::Size>& tileSizes,
                                                 const std::vector<cv::Point>& originalCorners, double scale,
                                                 std::vector<cv::Point>& smallCorners, std::vector<cv::UMat>& smallMasks,
                                                 cv::Mat& finalMosaic, cv::Mat& tileIndexMap) {
        const int tileCount = (int)tileSizes.size();
        if (tileCount == 0 || tileCount >= (int)NO_TILE) return -1;   // la carte de provenance est sur 8 bits (255 = aucune tuile)
        if ((int)tilePaths.size() != tileCount || (int)smallMasks.size() != tileCount ||
            (int)originalCorners.size() != tileCount || (int)smallCorners.size() != tileCount) return -1;

        // Application de l'algorithme GraphCut d'OpenCV (COST_COLOR) PAIRE PAR PAIRE, dans l'ordre (i, j>i) que le seam
        // finder utilise en interne. Les masques (8 bits) sont partagés avec la liste globale : la modification faite sur la
        // paire (i, j) est donc vue par les paires suivantes, comme dans l'appel unique d'origine. Les tuiles sont relues
        // depuis le disque : au plus DEUX en mémoire à un instant donné (i, puis chaque j qui la chevauche).
        // Coût couleur : la frontière est placée là où le passage d'une image à l'autre se remarque le moins.
        cv::Ptr<cv::detail::SeamFinder> seamFinder =
            cv::makePtr<cv::detail::GraphCutSeamFinder>(cv::detail::GraphCutSeamFinderBase::COST_COLOR);
        // Toutes les paires de tuiles
        for (int tileIndexI = 0; tileIndexI < tileCount - 1; ++tileIndexI) {
            cv::UMat imageI;   // chargée à la première paire qui l'utilise, libérée en fin de tour
            for (int tileIndexJ = tileIndexI + 1; tileIndexJ < tileCount; ++tileIndexJ) {
                cv::Rect overlapRectangle;
                // Paire dont les rectangles ne se chevauchent pas : ignorée
                if (!cv::detail::overlapRoi(smallCorners[tileIndexI], smallCorners[tileIndexJ], smallMasks[tileIndexI].size(),
                                            smallMasks[tileIndexJ].size(), overlapRectangle))
                    continue;
                if (imageI.empty()) {
                    imageI = loadSmallUMat(tilePaths[tileIndexI], scale, smallMasks[tileIndexI].size());
                    if (imageI.empty()) {
                        std::cout << "Impossible to read : " << tilePaths[tileIndexI] << std::endl;
                        return -1;
                    }
                }
                cv::UMat imageJ = loadSmallUMat(tilePaths[tileIndexJ], scale, smallMasks[tileIndexJ].size());
                if (imageJ.empty()) {
                    std::cout << "Impossible to read : " << tilePaths[tileIndexJ] << std::endl;
                    return -1;
                }
                // Paire qui se chevauche : les deux images réduites passent dans le seam finder
                std::vector<cv::UMat> pairImages{imageI, imageJ};
                std::vector<cv::Point> pairCorners{smallCorners[tileIndexI], smallCorners[tileIndexJ]};
                std::vector<cv::UMat> pairMasks{smallMasks[tileIndexI], smallMasks[tileIndexJ]};
                seamFinder->find(pairImages, pairCorners, pairMasks);
            }
        }

        // Taille de la toile (pleine résolution) et rectangles EXACTS des tuiles
        int canvasWidth = 0, canvasHeight = 0;
        std::vector<cv::Rect> tileRectangles(tileCount);
        for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
            tileRectangles[tileIndex] = cv::Rect(originalCorners[tileIndex], tileSizes[tileIndex]);
            canvasWidth = std::max(canvasWidth, tileRectangles[tileIndex].x + tileRectangles[tileIndex].width);
            canvasHeight = std::max(canvasHeight, tileRectangles[tileIndex].y + tileRectangles[tileIndex].height);
        }

        // Carte de provenance sur la grille RÉDUITE : quelle tuile a gardé chaque pixel après la coupe
        int smallWidth = 1, smallHeight = 1;
        for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
            const cv::Size smallSize = smallMasks[tileIndex].size();
            smallWidth = std::max(smallWidth, smallCorners[tileIndex].x + smallSize.width);
            smallHeight = std::max(smallHeight, smallCorners[tileIndex].y + smallSize.height);
        }
        // Image réduite dont les valeurs sont des numéros de tuile, et non des couleurs
        cv::Mat smallLabels(smallHeight, smallWidth, CV_8U, cv::Scalar(NO_TILE));
        for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
            const cv::Mat mask = smallMasks[tileIndex].getMat(cv::ACCESS_READ);
            for (int row = 0; row < mask.rows; ++row) {
                const uchar* maskRow = mask.ptr<uchar>(row);
                uchar* labelRow = smallLabels.ptr<uchar>(smallCorners[tileIndex].y + row) + smallCorners[tileIndex].x;
                // Même seuil que la version d'origine : si la tuile garde ce pixel et qu'aucune tuile ne l'a encore pris,
                // il lui est attribué
                for (int column = 0; column < mask.cols; ++column)
                    if (maskRow[column] > 127 && labelRow[column] == NO_TILE) labelRow[column] = (uchar)tileIndex;
            }
        }
        // Pixels réduits qu'aucune tuile n'a gardés (arrondis aux bords) : prennent la tuile du pixel gardé le plus proche
        {
            cv::Mat unclaimedMask = (smallLabels == NO_TILE), distances, nearestLabels;
            if (cv::countNonZero(unclaimedMask) > 0 && cv::countNonZero(unclaimedMask) < (int)unclaimedMask.total()) {
                // Pixels attribués les plus proches
                cv::distanceTransform(unclaimedMask, distances, nearestLabels, cv::DIST_L2, cv::DIST_MASK_5, cv::DIST_LABEL_PIXEL);
                double maximumLabel;
                cv::minMaxLoc(nearestLabels, nullptr, &maximumLabel);
                std::vector<uchar> tileByLabel((size_t)maximumLabel + 1, NO_TILE);
                for (int row = 0; row < smallHeight; ++row)
                    for (int column = 0; column < smallWidth; ++column)
                        if (!unclaimedMask.at<uchar>(row, column))
                            tileByLabel[nearestLabels.at<int>(row, column)] = smallLabels.at<uchar>(row, column);
                for (int row = 0; row < smallHeight; ++row)
                    for (int column = 0; column < smallWidth; ++column)
                        if (unclaimedMask.at<uchar>(row, column))
                            smallLabels.at<uchar>(row, column) = tileByLabel[nearestLabels.at<int>(row, column)];
            }
        }

        // Facteur de réduction réellement appliqué (même rapport sur le grand côté), estimé sur la première tuile en
        // tenant compte des arrondis du redimensionnement
        const cv::Size firstSmallSize = smallMasks[0].size();
        const double gridScale = (double)std::max(firstSmallSize.width, firstSmallSize.height) /
                                 (double)std::max(tileSizes[0].width, tileSizes[0].height);
        // smallColumnOf et smallRowOf : coordonnées pleine résolution → coordonnées réduites
        std::vector<int> smallColumnOf(canvasWidth), smallRowOf(canvasHeight);
        for (int column = 0; column < canvasWidth; ++column)
            smallColumnOf[column] = std::min(smallWidth - 1, (int)(column * gridScale));
        for (int row = 0; row < canvasHeight; ++row) smallRowOf[row] = std::min(smallHeight - 1, (int)(row * gridScale));

        // Carte de provenance EXACTE en pleine résolution : un seul numéro par pixel, parmi les tuiles qui le couvrent
        tileIndexMap = cv::Mat(cv::Size(canvasWidth, canvasHeight), CV_8U, cv::Scalar(NO_TILE));
        for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex) {   // 1) la tuile désignée par la coupe, si elle couvre le pixel
            const cv::Rect& tileRectangle = tileRectangles[tileIndex];
            for (int row = 0; row < tileRectangle.height; ++row) {
                uchar* indexRow = tileIndexMap.ptr<uchar>(tileRectangle.y + row) + tileRectangle.x;
                const uchar* labelRow = smallLabels.ptr<uchar>(smallRowOf[tileRectangle.y + row]);
                for (int column = 0; column < tileRectangle.width; ++column)
                    if (labelRow[smallColumnOf[tileRectangle.x + column]] == tileIndex) indexRow[column] = (uchar)tileIndex;
            }
        }
        std::vector<cv::Point2f> tileCenters(tileCount);
        for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex)
            tileCenters[tileIndex] = cv::Point2f(tileRectangles[tileIndex].x + tileRectangles[tileIndex].width / 2.f,
                                                 tileRectangles[tileIndex].y + tileRectangles[tileIndex].height / 2.f);
        fallbackPixelCount = 0;
        for (int row = 0; row < canvasHeight; ++row) {   // 2) pixel couvert mais non attribué (arrondis de la grille réduite)
            std::vector<int> rowTiles;
            for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex)
                if (row >= tileRectangles[tileIndex].y && row < tileRectangles[tileIndex].y + tileRectangles[tileIndex].height)
                    rowTiles.push_back(tileIndex);
            uchar* indexRow = tileIndexMap.ptr<uchar>(row);
            const uchar* labelRow = smallLabels.ptr<uchar>(smallRowOf[row]);
            for (int column = 0; column < canvasWidth; ++column) {
                if (indexRow[column] != NO_TILE) continue;
                const int wantedTile = labelRow[smallColumnOf[column]];   // tuile désignée par la coupe (qui ne couvre pas ce pixel)
                int bestTile = -1;
                float bestDistance = 1e30f;
                // On cherche une autre tuile qui couvre le pixel : la plus proche de la tuile désignée
                for (int tileIndex : rowTiles) {
                    if (column < tileRectangles[tileIndex].x ||
                        column >= tileRectangles[tileIndex].x + tileRectangles[tileIndex].width)
                        continue;
                    const float distance = wantedTile >= tileCount
                                               ? 0.f
                                               : (float)cv::norm(tileCenters[tileIndex] - tileCenters[wantedTile]);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestTile = tileIndex;
                    }
                }
                // fallbackPixelCount compte les pixels qui ont eu besoin de cette réparation
                if (bestTile >= 0) {
                    indexRow[column] = (uchar)bestTile;
                    ++fallbackPixelCount;
                }
            }
        }

        // Mosaïque visuelle (8 bits, affichage seulement) : les tuiles pleine résolution sont relues UNE À LA FOIS
        finalMosaic = cv::Mat::zeros(cv::Size(canvasWidth, canvasHeight), CV_8UC3);   // toile noire pleine résolution
        for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
            cv::Mat tile16Bit = ImagePretreatmentClass::read16Bit(tilePaths[tileIndex]);
            if (tile16Bit.empty() || tile16Bit.size() != tileSizes[tileIndex]) return -1;
            cv::Mat tile8Bit;
            tile16Bit.convertTo(tile8Bit, CV_8UC3, 255.0 / 65535.0);
            tile16Bit.release();
            const cv::Mat mask = (tileIndexMap(tileRectangles[tileIndex]) == tileIndex);   // pixels attribués à la tuile
            tile8Bit.copyTo(finalMosaic(tileRectangles[tileIndex]), mask);                 // copiés sur la toile
        }   // <- une seule tuile pleine résolution en mémoire à la fois
        return 1;
    }

    // Des pixels attribués aux formes : polygones de chaque tuile.
    std::vector<DataNs::TileTerritory> GraphCutSeamFinderClass::extractPolygons(const cv::Mat& tileIndexMap, int tileCount) {
        std::vector<DataNs::TileTerritory> territories;
        for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
            // Création d'un masque binaire strictement dédié à la tuile tileIndex
            const cv::Mat tileMask = (tileIndexMap == tileIndex);

            // Contours extérieurs ET trous (RETR_CCOMP) : sans les trous, le pavage ne serait pas exact.
            std::vector<std::vector<cv::Point>> contours;
            std::vector<cv::Vec4i> hierarchy;
            cv::findContours(tileMask, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);

            DataNs::TileTerritory territory;
            territory.tileIndex = tileIndex;
            for (size_t contourIndex = 0; contourIndex < contours.size(); ++contourIndex) {
                if (hierarchy[contourIndex][3] >= 0) continue;   // un trou : rattaché à son polygone ci-dessous
                std::vector<std::vector<cv::Point>> holes;
                for (int holeIndex = hierarchy[contourIndex][2]; holeIndex >= 0; holeIndex = hierarchy[holeIndex][0])
                    holes.push_back(contours[holeIndex]);
                // Aucun filtre d'aire : un contour dégénéré (1 pixel de large) fait partie du territoire
                territory.polygons.push_back(contours[contourIndex]);
                territory.holes.push_back(holes);
            }
            territories.push_back(territory);
        }
        return territories;
    }

    // Compare les polygones à la carte des pixels.
    SeamCheck GraphCutSeamFinderClass::verifyTiling(const std::vector<DataNs::TileTerritory>& territories,
                                                    const cv::Mat& tileIndexMap, const std::vector<cv::Size>& tileSizes,
                                                    const std::vector<cv::Point>& originalCorners) const {
        SeamCheck check;
        check.fallbackPixelCount = fallbackPixelCount;
        cv::Mat recomposed(tileIndexMap.size(), CV_8U, cv::Scalar(NO_TILE));   // carte d'étiquettes vide
        const cv::Rect canvas(0, 0, tileIndexMap.cols, tileIndexMap.rows);
        for (const DataNs::TileTerritory& territory : territories) {
            if (territory.polygons.empty()) continue;
            std::vector<cv::Point> allPoints;
            for (const auto& polygon : territory.polygons) allPoints.insert(allPoints.end(), polygon.begin(), polygon.end());
            const cv::Rect boundingBox = cv::boundingRect(allPoints) & canvas;
            if (boundingBox.area() <= 0) continue;
            cv::Mat territoryMask = cv::Mat::zeros(boundingBox.size(), CV_8U);
            const cv::Point offset(-boundingBox.x, -boundingBox.y);
            for (size_t polygonIndex = 0; polygonIndex < territory.polygons.size(); ++polygonIndex) {
                cv::drawContours(territoryMask, std::vector<std::vector<cv::Point>>{territory.polygons[polygonIndex]}, -1,
                                 cv::Scalar(255), cv::FILLED, cv::LINE_8, cv::noArray(), INT_MAX, offset);
                for (const auto& hole : territory.holes[polygonIndex]) {
                    const std::vector<std::vector<cv::Point>> holeContours{hole};
                    // Le trou est vidé…
                    cv::drawContours(territoryMask, holeContours, -1, cv::Scalar(0), cv::FILLED, cv::LINE_8, cv::noArray(),
                                     INT_MAX, offset);
                    // …mais son contour appartient au territoire
                    cv::drawContours(territoryMask, holeContours, -1, cv::Scalar(255), 1, cv::LINE_8, cv::noArray(), INT_MAX,
                                     offset);
                    ++check.holeCount;
                    check.vertexCount += (long)hole.size();
                }
                check.vertexCount += (long)territory.polygons[polygonIndex].size();
            }
            check.polygonCount += (int)territory.polygons.size();
            cv::Mat recomposedRegion = recomposed(boundingBox), alreadyFilled;
            cv::compare(recomposedRegion, cv::Scalar(NO_TILE), alreadyFilled, cv::CMP_NE);
            check.overlapPixelCount += cv::countNonZero(alreadyFilled & territoryMask);
            recomposedRegion.setTo(territory.tileIndex, territoryMask);
        }
        cv::Mat differences;
        cv::compare(recomposed, tileIndexMap, differences, cv::CMP_NE);
        check.differentPixelCount = cv::countNonZero(differences);
        check.isExactTiling = check.differentPixelCount == 0;   // la reconstruction redonne-t-elle la carte de provenance ?

        cv::Mat coveredMask = cv::Mat::zeros(tileIndexMap.size(), CV_8U);
        for (size_t tileIndex = 0; tileIndex < tileSizes.size(); ++tileIndex)
            coveredMask(cv::Rect(originalCorners[tileIndex], tileSizes[tileIndex])).setTo(255);
        check.gapPixelCount = cv::countNonZero((tileIndexMap == NO_TILE) & coveredMask);
        return check;
    }

}
