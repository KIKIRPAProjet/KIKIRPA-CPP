// src/backend/algorithms/rejection/ransac/ransac_rejector.cpp
#include "algorithms/rejection/ransac/ransac_rejector.h"

#include "algorithms/rejection/ransac/ransac_algorithm.h"
#include "algorithms/rejection/ransac/ransac_preprocessor.h"
#include "algorithms/rejection/ransac/ransac_types.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace RansacNs {

    namespace {

        // Conversion des paramètres JSON vers la structure propre à RANSAC, avec les bornes de chaque valeur.
        RansacParameters readParameters(const Json& parameters) {
            RansacParameters ransacParameters;
            // Nombre minimum de matches
            ransacParameters.minimumMatchCount = std::max(2, parameters.value("minMatches", 4));
            // Seuil de reprojection RANSAC
            ransacParameters.reprojectionThreshold = std::max(0.000001, parameters.value("ransacReprojThreshold", 3.0));
            // Nombre maximum d'itérations
            const int maximumIterationCount = parameters.value("maxIters", 2000);
            ransacParameters.maximumIterationCount = static_cast<std::size_t>(std::max(1, maximumIterationCount));
            // Niveau de confiance
            ransacParameters.confidence = std::clamp(parameters.value("confidence", 0.99), 0.000001, 0.999999);
            // Raffinement final
            const int refinementIterationCount = parameters.value("refineIters", 10);
            ransacParameters.refinementIterationCount = static_cast<std::size_t>(std::max(0, refinementIterationCount));
            // Nombre minimum d'inliers
            ransacParameters.minimumInlierCount = std::max(2, parameters.value("minInliers", 6));
            return ransacParameters;
        }

        // Médiane ; pour un nombre pair de valeurs, moyenne des deux valeurs du milieu.
        double computeMedian(std::vector<double>& values) {
            const auto middle = values.begin() + values.size() / 2;
            std::nth_element(values.begin(), middle, values.end());
            double medianValue = *middle;
            if (values.size() % 2 == 0) medianValue = 0.5 * (medianValue + *std::max_element(values.begin(), middle));
            return medianValue;
        }

    }

    RansacRejectorClass::RansacRejectorClass(const Json& initialParameters)
        : parameters(initialParameters.is_object() ? initialParameters : Json::object()) {}

    bool RansacRejectorClass::estimate(const DataNs::Matches& matches, DataNs::Alignment& alignment) {
        // 1. Nettoyage de la sortie du pipeline
        alignment.transformBToA.release();
        alignment.inlierMask.clear();

        // 2. Conversion des paramètres JSON vers une structure propre à RANSAC
        const RansacParameters ransacParameters = readParameters(parameters);

        // 3. Structure d'entrée. On cherche une transformation B -> A : source = pointsInB, destination = pointsInA.
        const RansacInput input{matches.pointsInB, matches.pointsInA};

        // 4. Prétraitement
        RansacPreprocessorClass preprocessor;
        RansacPreparedInput preparedInput;
        if (!preprocessor.prepare(input, ransacParameters, preparedInput)) return false;

        // 5. Algorithme RANSAC
        RansacAlgorithmClass algorithm;
        RansacOutput ransacOutput;
        if (!algorithm.estimate(preparedInput, ransacParameters, ransacOutput)) return false;

        // 6. Conversion de la sortie interne RANSAC vers la sortie générique du projet
        alignment.transformBToA = ransacOutput.transformBToA.clone();
        alignment.inlierMask = std::move(ransacOutput.inlierMask);
        return true;
    }

    // Principe du vote : sur un banc mécanique rigide (rail X-Y), les tuiles ne tournent pas.
    // Un modèle de pure translation 2D supprime le risque d'overfitting angulaire
    // (faux angle induit par le bruit sous-pixel sur une bande étroite de recouvrement).
    // L'algorithme s'exécute en temps linéaire O(N), est déterministe et résiste
    // jusqu'à 90 % d'aberrants (cf. Section 7 des notebooks KIK-IRPA 02_sift et 03_lightglue).
    VoteRejectorClass::VoteRejectorClass(const Json& initialParameters)
        : parameters(initialParameters.is_object() ? initialParameters : Json::object()) {}

    bool VoteRejectorClass::estimate(const DataNs::Matches& matches, DataNs::Alignment& alignment) {
        alignment.transformBToA.release();
        alignment.inlierMask.clear();

        // 1. Paramètres avec valeurs par défaut alignées sur les notebooks de recherche KIK-IRPA
        RansacParameters ransacParameters = readParameters(parameters);
        ransacParameters.minimumMatchCount = std::max(2, parameters.value("minMatches", 3));
        ransacParameters.minimumInlierCount = std::max(2, parameters.value("minInliers", 3));
        const double distanceThreshold = std::max(1e-6, parameters.value("seuil",
                                                                         parameters.value("ransacReprojThreshold", 1.5)));
        const double cellSize = std::max(1e-6, parameters.value("case", 1.0));   // taille de cellule de l'accumulateur (1 px)

        // 2. Prétraitement : réutilisation de RansacPreprocessorClass (filtre NaN/Inf et conserve originalIndices)
        RansacPreprocessorClass preprocessor;
        RansacPreparedInput preparedInput;
        if (!preprocessor.prepare({matches.pointsInB, matches.pointsInA}, ransacParameters, preparedInput)) return false;

        // 3. Accumulation 2D avec lissage par voisinage 3x3 (équivalent exact à cv2.blur(hist, (3, 3)))
        // Chaque point d_k = dest - src vote pour sa cellule (cellX, cellY) et ses 8 voisines directes.
        // L'usage de std::map sparse évite d'allouer une matrice dense géante si des faux appariements sont très éloignés.
        const std::size_t pointCount = preparedInput.sourcePoints.size();
        std::vector<double> translationsX(pointCount), translationsY(pointCount);
        std::map<std::pair<int, int>, int> voteCountByCell;
        for (std::size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
            translationsX[pointIndex] = static_cast<double>(preparedInput.destinationPoints[pointIndex].x) -
                                        preparedInput.sourcePoints[pointIndex].x;
            translationsY[pointIndex] = static_cast<double>(preparedInput.destinationPoints[pointIndex].y) -
                                        preparedInput.sourcePoints[pointIndex].y;
            const int cellX = static_cast<int>(std::floor(translationsX[pointIndex] / cellSize));
            const int cellY = static_cast<int>(std::floor(translationsY[pointIndex] / cellSize));
            for (int neighbourOffsetY = -1; neighbourOffsetY <= 1; ++neighbourOffsetY)
                for (int neighbourOffsetX = -1; neighbourOffsetX <= 1; ++neighbourOffsetX)
                    ++voteCountByCell[{cellY + neighbourOffsetY, cellX + neighbourOffsetX}];
        }

        // 4. Extraction déterministe du mode (la cellule ayant reçu le maximum de votes)
        auto bestCell = voteCountByCell.begin();
        for (auto cell = voteCountByCell.begin(); cell != voteCountByCell.end(); ++cell)
            if (cell->second > bestCell->second) bestCell = cell;

        double translationX = (bestCell->first.second + 0.5) * cellSize;
        double translationY = (bestCell->first.first + 0.5) * cellSize;

        // 5. Raffinement par médiane itérative (3 passes) sur les correspondances dans le rayon distanceThreshold
        // (1.5 px). Permet d'obtenir une précision sous-pixel insensible aux aberrants restés sous le seuil.
        for (int passIndex = 0; passIndex < 3; ++passIndex) {
            std::vector<double> inlierTranslationsX, inlierTranslationsY;
            for (std::size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex)
                if (std::hypot(translationsX[pointIndex] - translationX, translationsY[pointIndex] - translationY) <=
                    distanceThreshold) {
                    inlierTranslationsX.push_back(translationsX[pointIndex]);
                    inlierTranslationsY.push_back(translationsY[pointIndex]);
                }
            if (inlierTranslationsX.empty()) break;
            translationX = computeMedian(inlierTranslationsX);
            translationY = computeMedian(inlierTranslationsY);
        }

        // 6. Marquage final des inliers dans le repère des Matches d'origine
        alignment.inlierMask.assign(preparedInput.originalCount, 0);
        int inlierCount = 0;
        for (std::size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
            if (std::hypot(translationsX[pointIndex] - translationX, translationsY[pointIndex] - translationY) <=
                distanceThreshold) {
                alignment.inlierMask[preparedInput.originalIndices[pointIndex]] = 1;
                ++inlierCount;
            }
        }
        if (inlierCount < ransacParameters.minimumInlierCount) {
            alignment.inlierMask.assign(preparedInput.originalCount, 0);
            return false;
        }

        // 7. Matrice homogène de pure translation 3x3 (B -> A) : rotation parasite strictement nulle (theta = 0)
        alignment.transformBToA = cv::Mat::eye(3, 3, CV_64F);
        alignment.transformBToA.at<double>(0, 2) = translationX;
        alignment.transformBToA.at<double>(1, 2) = translationY;
        return true;
    }

}
