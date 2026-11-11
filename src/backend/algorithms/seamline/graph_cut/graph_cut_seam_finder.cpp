// src/backend/algorithms/seamline/graph_cut/graph_cut_seam_finder.cpp
#include "algorithms/seamline/graph_cut/graph_cut_seam_finder.h"

#include "algorithms/seamline/graph_cut/image_pretreatment.h"
#include "core/data/algorithm_types.h"

#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/cvstd_wrapper.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/stitching/detail/seam_finders.hpp>
#include <opencv2/stitching/detail/util.hpp>
#if __has_include(<opencv2/geometry/2d.hpp>)
#include <opencv2/geometry/2d.hpp>
#endif

#include <algorithm>
#include <climits>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace GraphCutNs {

    namespace {

        // Un pixel d'un masque réduit est gardé s'il dépasse ce seuil (même seuil que la version d'origine).
        constexpr uchar MASK_KEPT_THRESHOLD = 127;

        // Valeurs d'un pixel plein et d'un pixel vide dans un masque binaire.
        constexpr int MASK_FILLED = 255;
        constexpr int MASK_EMPTY = 0;

        // Épaisseur du trait qui redessine le contour d'un trou.
        constexpr int CONTOUR_THICKNESS = 1;

        // Plus grande que toute distance entre deux centres de tuiles : point de départ d'une recherche de minimum.
        constexpr float FAR_DISTANCE = 1e30f;

        // Le centre d'une tuile est à la moitié de sa largeur et de sa hauteur.
        constexpr float HALF = 0.5F;

        // Tuiles réduites : ce que la coupe de graphe lit. Les masques sont mis à jour sur place.
        struct ReducedTiles {
            const std::vector<std::string>& tilePaths;
            double scale;
            const std::vector<cv::Point>& corners;
            std::vector<cv::UMat>& masks;
        };

        // Rectangles exacts des tuiles dans la mosaïque pleine résolution, et leurs centres.
        struct TileGeometry {
            std::vector<cv::Rect> rects;
            std::vector<cv::Point2f> centers;
        };

        // Carte de provenance réduite, avec la correspondance entre coordonnées pleine résolution et coordonnées réduites.
        struct SmallGridView {
            const cv::Mat& labelMap;
            const std::vector<int>& smallXByFullX;
            const std::vector<int>& smallYByFullY;
        };

        // Relit une tuile et la convertit en UMat flottant 0–255 (exigée par le GraphCutSeamFinder d'OpenCV).
        // Renvoie une UMat vide si la lecture échoue ou si la taille ne correspond pas au masque réduit attendu.
        cv::UMat loadSmallUMat(const std::string& path, double scale, const cv::Size& expectedSize) {
            const cv::Mat smallImage = ImagePretreatmentClass::readSmallFloat(path, scale);
            cv::UMat smallUMat;
            if (smallImage.empty() || smallImage.size() != expectedSize)
                return smallUMat;
            smallImage.copyTo(smallUMat);
            return smallUMat;
        }

        // Charge la tuile réduite `tileIndex` dans `image`. Renvoie false si elle est illisible (cause dans errorMessage).
        bool loadReducedTile(const ReducedTiles& reducedTiles, std::size_t tileIndex, cv::UMat& image, std::string& errorMessage) {
            image = loadSmallUMat(reducedTiles.tilePaths[tileIndex], reducedTiles.scale, reducedTiles.masks[tileIndex].size());
            if (image.empty()) {
                errorMessage = "Impossible to read tile: " + reducedTiles.tilePaths[tileIndex];
                return false;
            }
            return true;
        }

        // Pour chaque tuile, les indices des tuiles suivantes (indice plus grand) qui la chevauchent, dans l'ordre croissant.
        // C'est exactement l'ensemble des paires (i, j > i) que le seam finder doit traiter.
        std::vector<std::vector<std::size_t>> findOverlappingPairs(const ReducedTiles& reducedTiles) {
            const std::size_t tileCount = reducedTiles.masks.size();
            std::vector<std::vector<std::size_t>> overlapsAfter(tileCount);
            for (std::size_t firstIndex = 0; firstIndex + 1 < tileCount; ++firstIndex) {
                for (std::size_t secondIndex = firstIndex + 1; secondIndex < tileCount; ++secondIndex) {
                    cv::Rect overlap;
                    if (cv::detail::overlapRoi(reducedTiles.corners[firstIndex], reducedTiles.corners[secondIndex],
                                               reducedTiles.masks[firstIndex].size(), reducedTiles.masks[secondIndex].size(),
                                               overlap))
                        overlapsAfter[firstIndex].push_back(secondIndex);
                }
            }
            return overlapsAfter;
        }

        // Pour chaque tuile, le tour de la boucle externe (indice `firstIndex`) à la fin duquel elle ne sert plus.
        // Une tuile sert au tour `firstIndex` si c'est elle (en première position) ou une de ses voisines suivantes
        // qui y est traitée. Vaut NOT_USED si elle ne chevauche aucune autre tuile (elle n'est alors jamais lue).
        constexpr std::size_t NOT_USED = std::numeric_limits<std::size_t>::max();

        std::vector<std::size_t> findLastRound(const std::vector<std::vector<std::size_t>>& overlapsAfter) {
            std::vector<std::size_t> lastRound(overlapsAfter.size(), NOT_USED);
            for (std::size_t firstIndex = 0; firstIndex < overlapsAfter.size(); ++firstIndex) {
                if (overlapsAfter[firstIndex].empty())
                    continue;
                lastRound[firstIndex] = firstIndex;
                for (std::size_t secondIndex : overlapsAfter[firstIndex])
                    lastRound[secondIndex] = firstIndex;   // les tours sont croissants : la dernière écriture est la plus grande
            }
            return lastRound;
        }

        // Applique la coupe de graphe d'OpenCV à chaque paire de tuiles qui se chevauchent, dans l'ordre (i, j > i) que le
        // seam finder utilise en interne. Les masques sont partagés avec la liste globale : la modification faite sur la
        // paire (i, j) est donc vue par les paires suivantes, comme dans l'appel unique d'origine.
        //
        // Chaque tuile est lue et réduite une seule fois : elle est chargée à sa première utilisation, gardée tant qu'une
        // paire à venir en a besoin, puis libérée dès la fin du dernier tour où elle sert. Pour une grille de tuiles
        // parcourue ligne par ligne, seules les tuiles voisines de la ligne courante restent en mémoire (environ une
        // ligne de tuiles réduites, plus deux).
        // Renvoie false si une tuile est illisible (cause dans errorMessage).
        bool cutSeamsPairByPair(ReducedTiles& reducedTiles, std::string& errorMessage) {
            // Le coût couleur place la frontière là où passer d'une image à l'autre se voit le moins.
            cv::Ptr<cv::detail::SeamFinder> seamFinder =
                cv::makePtr<cv::detail::GraphCutSeamFinder>(cv::detail::GraphCutSeamFinderBase::COST_COLOR);
            const std::size_t tileCount = reducedTiles.masks.size();
            const std::vector<std::vector<std::size_t>> overlapsAfter = findOverlappingPairs(reducedTiles);
            const std::vector<std::size_t> lastRound = findLastRound(overlapsAfter);
            std::vector<cv::UMat> loadedImages(tileCount);   // vide tant que la tuile n'est pas chargée, ou déjà libérée

            for (std::size_t firstIndex = 0; firstIndex + 1 < tileCount; ++firstIndex) {
                for (std::size_t secondIndex : overlapsAfter[firstIndex]) {
                    for (std::size_t tileIndex : {firstIndex, secondIndex})
                        if (loadedImages[tileIndex].empty() &&
                            !loadReducedTile(reducedTiles, tileIndex, loadedImages[tileIndex], errorMessage))
                            return false;

                    std::vector<cv::UMat> pairImages{loadedImages[firstIndex], loadedImages[secondIndex]};
                    std::vector<cv::Point> pairCorners{reducedTiles.corners[firstIndex], reducedTiles.corners[secondIndex]};
                    std::vector<cv::UMat> pairMasks{reducedTiles.masks[firstIndex], reducedTiles.masks[secondIndex]};
                    seamFinder->find(pairImages, pairCorners, pairMasks);
                }
                // Fin du tour : libère les tuiles qui ne serviront plus. Une tuile libérée n'est jamais relue, car son
                // dernier tour est passé.
                for (std::size_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
                    if (lastRound[tileIndex] == firstIndex)
                        loadedImages[tileIndex].release();
            }
            return true;
        }

        // Rectangle exact et centre de chaque tuile dans la mosaïque pleine résolution.
        TileGeometry buildTileGeometry(const std::vector<cv::Point>& originalCorners, const std::vector<cv::Size>& tileSizes) {
            TileGeometry geometry;
            for (std::size_t tileIndex = 0; tileIndex < tileSizes.size(); ++tileIndex) {
                const cv::Rect tileRect(originalCorners[tileIndex], tileSizes[tileIndex]);
                geometry.rects.push_back(tileRect);
                geometry.centers.push_back(cv::Point2f(static_cast<float>(tileRect.x) + static_cast<float>(tileRect.width) * HALF,
                                                       static_cast<float>(tileRect.y) + static_cast<float>(tileRect.height) * HALF));
            }
            return geometry;
        }

        // Taille de la toile pleine résolution : le plus petit rectangle, ancré à l'origine, qui contient toutes les tuiles.
        cv::Size computeCanvasSize(const std::vector<cv::Rect>& tileRects) {
            int canvasWidth = 0;
            int canvasHeight = 0;
            for (const cv::Rect& tileRect : tileRects) {
                canvasWidth = std::max(canvasWidth, tileRect.x + tileRect.width);
                canvasHeight = std::max(canvasHeight, tileRect.y + tileRect.height);
            }
            return cv::Size(canvasWidth, canvasHeight);
        }

        // Taille de la grille réduite : le plus petit rectangle, ancré à l'origine, qui contient tous les masques réduits
        // (au moins 1 x 1).
        cv::Size computeSmallGridSize(const ReducedTiles& reducedTiles) {
            int gridWidth = 1;
            int gridHeight = 1;
            for (std::size_t tileIndex = 0; tileIndex < reducedTiles.masks.size(); ++tileIndex) {
                const cv::Size maskSize = reducedTiles.masks[tileIndex].size();
                gridWidth = std::max(gridWidth, reducedTiles.corners[tileIndex].x + maskSize.width);
                gridHeight = std::max(gridHeight, reducedTiles.corners[tileIndex].y + maskSize.height);
            }
            return cv::Size(gridWidth, gridHeight);
        }

        // Carte de provenance sur la grille réduite : quelle tuile a gardé chaque pixel après la coupe (NO_TILE = aucune).
        // Un pixel gardé par plusieurs tuiles va à la première.
        cv::Mat buildSmallLabelMap(const ReducedTiles& reducedTiles) {
            const cv::Size gridSize = computeSmallGridSize(reducedTiles);
            cv::Mat smallLabelMap(gridSize.height, gridSize.width, CV_8U, cv::Scalar(NO_TILE));
            for (std::size_t tileIndex = 0; tileIndex < reducedTiles.masks.size(); ++tileIndex) {
                const cv::Point& corner = reducedTiles.corners[tileIndex];
                const cv::Mat mask = reducedTiles.masks[tileIndex].getMat(cv::ACCESS_READ);
                for (int y = 0; y < mask.rows; ++y) {
                    const uchar* maskRow = mask.ptr<uchar>(y);
                    uchar* labelRow = smallLabelMap.ptr<uchar>(corner.y + y) + corner.x;
                    for (int x = 0; x < mask.cols; ++x)
                        if (maskRow[x] > MASK_KEPT_THRESHOLD && labelRow[x] == NO_TILE)
                            labelRow[x] = static_cast<uchar>(tileIndex);
                }
            }
            return smallLabelMap;
        }

        // Pixels réduits qu'aucune tuile n'a gardés (arrondis aux bords) : ils prennent la tuile du pixel gardé le plus proche.
        void assignUnclaimedPixelsToNearestTile(cv::Mat& smallLabelMap) {
            const cv::Mat unclaimed = (smallLabelMap == NO_TILE);
            const int unclaimedCount = cv::countNonZero(unclaimed);
            // Sans pixel libre il n'y a rien à faire ; si tous sont libres, il n'y a aucune tuile à copier.
            if (unclaimedCount == 0 || unclaimedCount >= static_cast<int>(unclaimed.total()))
                return;

            cv::Mat distances;
            cv::Mat labels;
            cv::distanceTransform(unclaimed, distances, labels, cv::DIST_L2, cv::DIST_MASK_5, cv::DIST_LABEL_PIXEL);
            double maxLabel = 0;
            cv::minMaxLoc(labels, nullptr, &maxLabel);

            // Un label de distanceTransform désigne un pixel gardé ; on retient la tuile de ce pixel.
            std::vector<uchar> tileByLabel(static_cast<std::size_t>(maxLabel) + 1, NO_TILE);
            for (int y = 0; y < smallLabelMap.rows; ++y)
                for (int x = 0; x < smallLabelMap.cols; ++x)
                    if (!unclaimed.at<uchar>(y, x))
                        tileByLabel[labels.at<int>(y, x)] = smallLabelMap.at<uchar>(y, x);
            for (int y = 0; y < smallLabelMap.rows; ++y)
                for (int x = 0; x < smallLabelMap.cols; ++x)
                    if (unclaimed.at<uchar>(y, x))
                        smallLabelMap.at<uchar>(y, x) = tileByLabel[labels.at<int>(y, x)];
        }

        // Facteur de réduction réellement appliqué : mesuré sur la première tuile (même rapport sur le grand côté), car
        // la réduction arrondit les tailles.
        double computeGridScale(const cv::UMat& firstMask, const cv::Size& firstTileSize) {
            const cv::Size maskSize = firstMask.size();
            return static_cast<double>(std::max(maskSize.width, maskSize.height)) /
                   static_cast<double>(std::max(firstTileSize.width, firstTileSize.height));
        }

        // Un axe (horizontal ou vertical) de la mosaïque : longueur pleine résolution, longueur de la grille réduite et
        // facteur de réduction appliqué.
        struct AxisMapping {
            int fullLength;
            int smallLength;
            double gridScale;
        };

        // Pour chaque coordonnée pleine résolution (0 à fullLength - 1), la coordonnée correspondante sur la grille réduite.
        std::vector<int> buildSmallCoordinateMap(const AxisMapping& axis) {
            std::vector<int> smallByFull(axis.fullLength);
            for (int full = 0; full < axis.fullLength; ++full)
                smallByFull[full] = std::min(axis.smallLength - 1, static_cast<int>(full * axis.gridScale));
            return smallByFull;
        }

        // 1) Chaque pixel prend la tuile désignée par la coupe, si cette tuile le couvre.
        void assignCutTiles(const std::vector<cv::Rect>& tileRects, const SmallGridView& smallGrid, cv::Mat& indexMatrix) {
            for (std::size_t tileIndex = 0; tileIndex < tileRects.size(); ++tileIndex) {
                const cv::Rect& tileRect = tileRects[tileIndex];
                for (int y = 0; y < tileRect.height; ++y) {
                    uchar* indexRow = indexMatrix.ptr<uchar>(tileRect.y + y) + tileRect.x;
                    const uchar* labelRow = smallGrid.labelMap.ptr<uchar>(smallGrid.smallYByFullY[tileRect.y + y]);
                    for (int x = 0; x < tileRect.width; ++x)
                        if (labelRow[smallGrid.smallXByFullX[tileRect.x + x]] == tileIndex)
                            indexRow[x] = static_cast<uchar>(tileIndex);
                }
            }
        }

        // Tuiles qui couvrent la ligne `y` de la mosaïque.
        std::vector<std::size_t> findTilesCoveringRow(const std::vector<cv::Rect>& tileRects, int y) {
            std::vector<std::size_t> rowTiles;
            for (std::size_t tileIndex = 0; tileIndex < tileRects.size(); ++tileIndex)
                if (y >= tileRects[tileIndex].y && y < tileRects[tileIndex].y + tileRects[tileIndex].height)
                    rowTiles.push_back(tileIndex);
            return rowTiles;
        }

        // Pixel à attribuer : sa colonne `x`, les tuiles qui couvrent sa ligne et la tuile désignée par la coupe.
        struct UncutPixel {
            const std::vector<std::size_t>& rowTiles;
            int x;
            std::size_t wantedTile;
        };

        // Parmi les tuiles de `pixel.rowTiles` qui couvrent la colonne `pixel.x`, celle dont le centre est le plus proche de
        // celui de la tuile désignée par la coupe. Renvoie std::nullopt si aucune ne couvre ce pixel.
        // Si la coupe ne désigne aucune tuile (NO_TILE), toutes les distances valent 0 : la première tuile qui couvre l'emporte.
        std::optional<std::size_t> findNearestCoveringTile(const TileGeometry& geometry, const UncutPixel& pixel) {
            std::optional<std::size_t> nearestTile;
            float nearestDistance = FAR_DISTANCE;
            for (std::size_t tileIndex : pixel.rowTiles) {
                const cv::Rect& tileRect = geometry.rects[tileIndex];
                if (pixel.x < tileRect.x || pixel.x >= tileRect.x + tileRect.width)
                    continue;
                const float distance = pixel.wantedTile >= geometry.rects.size()
                    ? 0.F
                    : static_cast<float>(cv::norm(geometry.centers[tileIndex] - geometry.centers[pixel.wantedTile]));
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearestTile = tileIndex;
                }
            }
            return nearestTile;
        }

        // 2) Pixel couvert mais non attribué (arrondis de la grille réduite) : il prend, parmi les tuiles qui le couvrent, la
        // plus proche de la tuile désignée par la coupe. Traite la ligne `y` et renvoie le nombre de pixels réattribués.
        long assignUncutPixelsInRow(const TileGeometry& geometry, const SmallGridView& smallGrid, cv::Mat& indexMatrix, int y) {
            long reassignedPixelCount = 0;
            const std::vector<std::size_t> rowTiles = findTilesCoveringRow(geometry.rects, y);
            uchar* indexRow = indexMatrix.ptr<uchar>(y);
            const uchar* labelRow = smallGrid.labelMap.ptr<uchar>(smallGrid.smallYByFullY[y]);
            for (int x = 0; x < indexMatrix.cols; ++x) {
                if (indexRow[x] != NO_TILE)
                    continue;
                const std::size_t wantedTile = labelRow[smallGrid.smallXByFullX[x]];   // ne couvre pas ce pixel
                const std::optional<std::size_t> nearestTile = findNearestCoveringTile(geometry, UncutPixel{rowTiles, x, wantedTile});
                if (nearestTile) {
                    indexRow[x] = static_cast<uchar>(*nearestTile);
                    ++reassignedPixelCount;
                }
            }
            return reassignedPixelCount;
        }

        // Applique l'étape 2) à toute la mosaïque. Renvoie le nombre de pixels réattribués.
        long assignUncutPixels(const TileGeometry& geometry, const SmallGridView& smallGrid, cv::Mat& indexMatrix) {
            long reassignedPixelCount = 0;
            for (int y = 0; y < indexMatrix.rows; ++y)
                reassignedPixelCount += assignUncutPixelsInRow(geometry, smallGrid, indexMatrix, y);
            return reassignedPixelCount;
        }

        // Carte de provenance exacte en pleine résolution : un seul numéro par pixel, parmi les tuiles qui le couvrent.
        // Renvoie le nombre de pixels réattribués à une tuile voisine (arrondis de la grille réduite).
        long buildIndexMatrix(const TileGeometry& geometry, const cv::Mat& smallLabelMap, double gridScale, cv::Mat& indexMatrix) {
            const cv::Size canvasSize = computeCanvasSize(geometry.rects);
            const std::vector<int> smallXByFullX = buildSmallCoordinateMap({canvasSize.width, smallLabelMap.cols, gridScale});
            const std::vector<int> smallYByFullY = buildSmallCoordinateMap({canvasSize.height, smallLabelMap.rows, gridScale});
            const SmallGridView smallGrid{smallLabelMap, smallXByFullX, smallYByFullY};

            indexMatrix = cv::Mat(canvasSize, CV_8U, cv::Scalar(NO_TILE));
            assignCutTiles(geometry.rects, smallGrid, indexMatrix);
            return assignUncutPixels(geometry, smallGrid, indexMatrix);
        }

        // Dessine dans un masque local (rectangle `box`) les polygones du territoire, trous vidés, et compte polygones,
        // trous et sommets dans `check`.
        cv::Mat drawTerritoryMask(const DataNs::TileTerritory& territory, const cv::Rect& box, SeamCheck& check) {
            cv::Mat mask = cv::Mat::zeros(box.size(), CV_8U);
            const cv::Point offset(-box.x, -box.y);
            for (std::size_t polygonIndex = 0; polygonIndex < territory.polygons.size(); ++polygonIndex) {
                const std::vector<cv::Point>& polygon = territory.polygons[polygonIndex];
                cv::drawContours(mask, std::vector<std::vector<cv::Point>>{polygon}, -1, cv::Scalar(MASK_FILLED), cv::FILLED,
                                 cv::LINE_8, cv::noArray(), INT_MAX, offset);
                for (const std::vector<cv::Point>& hole : territory.holes[polygonIndex]) {
                    const std::vector<std::vector<cv::Point>> holeContours{hole};
                    // Le trou est vidé…
                    cv::drawContours(mask, holeContours, -1, cv::Scalar(MASK_EMPTY), cv::FILLED, cv::LINE_8, cv::noArray(),
                                     INT_MAX, offset);
                    // …mais son contour appartient au territoire.
                    cv::drawContours(mask, holeContours, -1, cv::Scalar(MASK_FILLED), CONTOUR_THICKNESS, cv::LINE_8,
                                     cv::noArray(), INT_MAX, offset);
                    ++check.holeCount;
                    check.vertexCount += static_cast<long>(hole.size());
                }
                check.vertexCount += static_cast<long>(polygon.size());
            }
            return mask;
        }

        // Ajoute le territoire à la carte redessinée `recomposed` et met à jour les compteurs de `check`, chevauchements compris.
        void drawTerritory(const DataNs::TileTerritory& territory, cv::Mat& recomposed, SeamCheck& check) {
            if (territory.polygons.empty())
                return;
            std::vector<cv::Point> allPoints;
            for (const std::vector<cv::Point>& polygon : territory.polygons)
                allPoints.insert(allPoints.end(), polygon.begin(), polygon.end());
            const cv::Rect canvas(0, 0, recomposed.cols, recomposed.rows);
            const cv::Rect box = cv::boundingRect(allPoints) & canvas;
            if (box.area() <= 0)
                return;

            const cv::Mat territoryMask = drawTerritoryMask(territory, box, check);
            check.polygonCount += static_cast<int>(territory.polygons.size());
            cv::Mat recomposedPart = recomposed(box);
            cv::Mat alreadyDrawn;
            cv::compare(recomposedPart, cv::Scalar(NO_TILE), alreadyDrawn, cv::CMP_NE);
            check.overlapPixelCount += cv::countNonZero(alreadyDrawn & territoryMask);
            recomposedPart.setTo(territory.tileIndex, territoryMask);
        }

        // Nombre de pixels couverts par le rectangle d'une tuile mais attribués à aucune.
        long countGapPixels(const cv::Mat& indexMatrix,
                            const std::vector<cv::Size>& tileSizes,
                            const std::vector<cv::Point>& originalCorners) {
            cv::Mat covered = cv::Mat::zeros(indexMatrix.size(), CV_8U);
            for (std::size_t tileIndex = 0; tileIndex < tileSizes.size(); ++tileIndex)
                covered(cv::Rect(originalCorners[tileIndex], tileSizes[tileIndex])).setTo(MASK_FILLED);
            return cv::countNonZero((indexMatrix == NO_TILE) & covered);
        }

    }

bool GraphCutSeamFinderClass::computeSeamline(const std::vector<std::string>& tilePaths,
                                              const FullResolutionGeometry& fullResolution,
                                              ReducedGrid& reducedGrid,
                                              cv::Mat& indexMatrix) {
    const std::vector<cv::Size>& tileSizes = fullResolution.tileSizes;
    const std::vector<cv::Point>& originalCorners = fullResolution.originalCorners;
    const double scale = reducedGrid.scale;
    const std::vector<cv::Point>& cornersSmall = reducedGrid.cornersSmall;
    std::vector<cv::UMat>& masksUmatSmall = reducedGrid.masksUmatSmall;

    errorMessage.clear();
        const std::size_t tileCount = tileSizes.size();
        if (tileCount == 0 || tileCount >= static_cast<std::size_t>(NO_TILE)) {
            errorMessage = "Tile count must be between 1 and " + std::to_string(NO_TILE - 1) + ", got " + std::to_string(tileCount);
            return false;
        }
        if (tilePaths.size() != tileCount || masksUmatSmall.size() != tileCount ||
            originalCorners.size() != tileCount || cornersSmall.size() != tileCount) {
            errorMessage = "Tile paths, corners and masks must have one entry per tile size";
            return false;
        }

        ReducedTiles reducedTiles{tilePaths, scale, cornersSmall, masksUmatSmall};
        if (!cutSeamsPairByPair(reducedTiles, errorMessage))
            return false;

        cv::Mat smallLabelMap = buildSmallLabelMap(reducedTiles);
        assignUnclaimedPixelsToNearestTile(smallLabelMap);

        const TileGeometry geometry = buildTileGeometry(originalCorners, tileSizes);
        const double gridScale = computeGridScale(masksUmatSmall[0], tileSizes[0]);
        fallbackPixelCount = buildIndexMatrix(geometry, smallLabelMap, gridScale, indexMatrix);
        return true;
    }

    std::vector<DataNs::TileTerritory> GraphCutSeamFinderClass::extractPolygons(const cv::Mat& indexMatrix, const std::vector<cv::Rect>& tileRects) const {
        const cv::Rect canvas(0, 0, indexMatrix.cols, indexMatrix.rows);
        std::vector<DataNs::TileTerritory> territories;
        territories.reserve(tileRects.size());
        for (std::size_t tileIndex = 0; tileIndex < tileRects.size(); ++tileIndex) {
            DataNs::TileTerritory territory;
            territory.tileIndex = static_cast<int>(tileIndex);
            const cv::Rect searchRect = tileRects[tileIndex] & canvas; // On ne regarde que ce rectangle (tuile), et non toute la mosaïque, pour limiter la croissance du coût.
            if (searchRect.area() <= 0) {
                territories.push_back(territory);
                continue;
            }
            const cv::Mat tileMask = (indexMatrix(searchRect) == static_cast<int>(tileIndex)); // Masque binaire strictement dédié à la tuile courante, limité à son rectangle.
            std::vector<std::vector<cv::Point>> contours;
            std::vector<cv::Vec4i> hierarchy;
            cv::findContours(tileMask, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE, searchRect.tl()); // Extérieurs ET trous (RETR_CCOMP) pour pavage exact. `searchRect.tl()` remet ds le repère mosaïque.
            for (std::size_t contourIndex = 0; contourIndex < contours.size(); ++contourIndex) {
                if (hierarchy[contourIndex][3] >= 0)
                    continue;   // un trou : rattaché à son polygone ci-dessous
                std::vector<std::vector<cv::Point>> holes;
                for (int holeIndex = hierarchy[contourIndex][2]; holeIndex >= 0; holeIndex = hierarchy[holeIndex][0])
                    holes.push_back(contours[holeIndex]);
                territory.polygons.push_back(contours[contourIndex]); // Aucun filtre d'aire : un contour dégénéré (1 pixel de large) fait partie du territoire.
                territory.holes.push_back(holes);
            }
            territories.push_back(territory);
        }
        return territories;
    }
}
