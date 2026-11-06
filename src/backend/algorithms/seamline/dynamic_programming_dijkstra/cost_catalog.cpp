// src/backend/algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace DynamicProgrammingDijkstraNs {

    namespace {

        // Outils de la section 3 nécessaires au catalogue

        // Valeur maximale positive du type (np.iinfo(dtype).max), 1 pour les flottants.
        double getMaximumValueOfType(const cv::Mat& image) {
            switch (image.depth()) {
                case CV_8U: return 255.0;
                case CV_8S: return 127.0;
                case CV_16U: return 65535.0;
                case CV_16S: return 32767.0;
                case CV_32S: return 2147483647.0;
                default: return 1.0;
            }
        }

        // to_float01 puis passage en gris : CV_32F, valeurs dans [0, 1].
        cv::Mat convertToUnitGrey(const cv::Mat& image) {
            cv::Mat floatImage;
            image.convertTo(floatImage, CV_MAKETYPE(CV_32F, image.channels()), 1.0 / getMaximumValueOfType(image));
            floatImage = cv::max(floatImage, 0.0);
            floatImage = cv::min(floatImage, 1.0);
            if (floatImage.channels() == 1) return floatImage;
            cv::Mat grey;
            cv::cvtColor(floatImage, grey, cv::COLOR_RGB2GRAY);   // RGB vers niveaux de gris
            return grey;
        }

        // Équivalent de np.percentile (interpolation linéaire) : renvoie la valeur.
        double computePercentileValue(const cv::Mat& matrix, double percentileRank) {
            cv::Mat flatValues;
            matrix.reshape(1, 1).convertTo(flatValues, CV_64F);   // matrice aplatie en une liste de doubles
            std::vector<double> values(flatValues.begin<double>(), flatValues.end<double>());
            const double rank = percentileRank / 100.0 * double(values.size() - 1);   // position du percentile
            const size_t lowerIndex = size_t(std::floor(rank));
            const size_t upperIndex = std::min(lowerIndex + 1, values.size() - 1);
            std::nth_element(values.begin(), values.begin() + lowerIndex, values.end());   // valeurs encadrantes
            const double lowerValue = values[lowerIndex];
            const double upperValue = (upperIndex == lowerIndex)
                                          ? lowerValue
                                          : *std::min_element(values.begin() + lowerIndex + 1, values.end());
            return lowerValue + (upperValue - lowerValue) * (rank - double(lowerIndex));   // interpolation si besoin
        }

        // Ajoute une pénalité au coût traité, en restant en CV_32F comme le Python.
        CostMaps addToTreated(CostMaps maps, const cv::Mat& penalty) {
            cv::Mat floatPenalty;
            penalty.convertTo(floatPenalty, CV_32F);
            maps.treated = maps.treated + floatPenalty;
            return maps;
        }

        // Exprime chaque pixel par rapport à la luminosité et au contraste de son voisinage.
        cv::Mat normalizeLocally(const cv::Mat& grey, int windowSize) {
            cv::Mat localMean, localMeanSquare;
            cv::blur(grey, localMean, cv::Size(windowSize, windowSize));                      // moyenne locale
            cv::blur(grey.mul(grey), localMeanSquare, cv::Size(windowSize, windowSize));      // moyenne locale des carrés
            cv::Mat localVariance = localMeanSquare - localMean.mul(localMean);              // variance locale
            localVariance = cv::max(localVariance, 1e-4);   // empêche la variance de devenir trop petite
            cv::Mat localStandardDeviation;
            cv::sqrt(localVariance, localStandardDeviation);
            return (grey - localMean) / localStandardDeviation;   // image normalisée localement
        }

    }

    // Fonctions du catalogue : overlapA = première image, overlapB = zone correspondante de la seconde,
    // parameters = réglages du calcul de coût.

    // Calcul de coût standard avec les réglages fournis.
    CostMaps computeColorGradientCost(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters) {
        return computeCostMaps(overlapA, overlapB, parameters);
    }

    CostMaps computeColorOnlyCost(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters) {
        CostMapParameters colorOnlyParameters = parameters;
        colorOnlyParameters.colorWeight = 1.0;   // poids de la couleur à 1
        return computeCostMaps(overlapA, overlapB, colorOnlyParameters);
    }

    CostMaps computeGradientOnlyCost(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters) {
        CostMapParameters gradientOnlyParameters = parameters;
        gradientOnlyParameters.colorWeight = 0.0;   // poids de la couleur à 0
        return computeCostMaps(overlapA, overlapB, gradientOnlyParameters);
    }

    // Floute les deux images avant le coût standard : les petites variations (bruit du capteur, grain) ne guident
    // plus la couture.
    CostMaps computeGrainRobustCost(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters,
                                    double blurSigma) {
        // Flou en unités brutes : le grain du capteur ne guide plus le chemin. blurSigma règle la force du flou.
        cv::Mat floatOverlapA, floatOverlapB;
        overlapA.convertTo(floatOverlapA, CV_MAKETYPE(CV_32F, overlapA.channels()));
        overlapB.convertTo(floatOverlapB, CV_MAKETYPE(CV_32F, overlapB.channels()));
        cv::GaussianBlur(floatOverlapA, floatOverlapA, cv::Size(0, 0), blurSigma);
        cv::GaussianBlur(floatOverlapB, floatOverlapB, cv::Size(0, 0), blurSigma);
        return computeCostMaps(floatOverlapA, floatOverlapB, parameters);
    }

    CostMaps computeLocalNormalizationCost(const cv::Mat& overlapA, const cv::Mat& overlapB,
                                           const CostMapParameters& parameters) {
        // Égalisation locale (moyenne et écart-type par fenêtre) : insensible aux variations d'éclairage.
        const cv::Mat greyA = convertToUnitGrey(overlapA), greyB = convertToUnitGrey(overlapB);
        const int windowSize = std::max(5, (std::min(greyA.rows, greyA.cols) / 8) | 1);   // taille du voisinage
        const cv::Mat normalizedA = normalizeLocally(greyA, windowSize), normalizedB = normalizeLocally(greyB, windowSize);

        cv::Mat colorTerm = cv::abs(normalizedA - normalizedB);   // écart entre les deux images normalisées
        colorTerm = cv::min(colorTerm, computePercentileValue(colorTerm, 99));   // écrête les valeurs aberrantes
        // Force moyenne du gradient
        cv::Mat gradientTerm = 0.5 * (computeGradientMagnitude(normalizedA) + computeGradientMagnitude(normalizedB));
        gradientTerm = cv::min(gradientTerm, computePercentileValue(gradientTerm, 99));
        colorTerm = normalizeMinMax(colorTerm);
        gradientTerm = normalizeMinMax(gradientTerm);

        cv::Mat weightedColorTerm;
        cv::Mat(colorTerm * parameters.colorWeight).convertTo(weightedColorTerm, CV_64F);
        const cv::Mat combined = weightedColorTerm + (1.0 - parameters.colorWeight) * gradientTerm;

        CostMaps maps;
        maps.colorTerm = colorTerm;                            // CV_32F, écarts d'apparence
        gradientTerm.convertTo(maps.gradientTerm, CV_32F);     // variations d'intensité
        combined.convertTo(maps.treated, CV_32F);              // carte de coût finale, qui guide la couture
        return maps;
    }

    // Pénalité dans les zones claires : la couture évite de traverser les reflets.
    CostMaps computeHighlightPenaltyCost(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters,
                                         double threshold, double weight) {
        // Pénalité sur les pixels proches de la saturation dans A ou B : la seamline contourne les reflets de vernis.
        cv::Mat brightest;
        cv::max(convertToUnitGrey(overlapA), convertToUnitGrey(overlapB), brightest);   // gris le plus clair des deux
        cv::Mat penalty = (brightest - threshold) / (1.0 - threshold);                 // pénalité ramenée dans [0, 1]
        penalty = cv::max(penalty, 0.0);
        penalty = cv::min(penalty, 1.0);
        return addToTreated(computeCostMaps(overlapA, overlapB, parameters), weight * penalty);
    }

    // Pénalité qui croît vers les bords gauche et droit.
    CostMaps computeCenterPriorCost(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters,
                                    double weight) {
        // Pénalité quadratique de l'écart au centre : sur un fond uni, la seamline reste droite et centrée.
        const int width = overlapA.cols;
        cv::Mat prior(1, width, CV_32F);
        for (int column = 0; column < width; ++column) {
            // Pour chaque colonne : position entre -1 et 1, au carré
            const float position = (width == 1) ? -1.0f : float(-1.0 + 2.0 * column / (width - 1));   // np.linspace(-1, 1, w)
            prior.at<float>(0, column) = position * position;
        }
        // La ligne de pénalité est répétée sur toute la hauteur de l'image
        return addToTreated(computeCostMaps(overlapA, overlapB, parameters), weight * cv::repeat(prior, overlapA.rows, 1));
    }

    // Sélection du mode

    CostMaps computeCost(const cv::Mat& overlapA, const cv::Mat& overlapB, CostFunction costFunction,
                         const CostMapParameters& parameters) {
        switch (costFunction) {
            case CostFunction::ColorGradient: return computeColorGradientCost(overlapA, overlapB, parameters);
            case CostFunction::ColorOnly: return computeColorOnlyCost(overlapA, overlapB, parameters);
            case CostFunction::GradientOnly: return computeGradientOnlyCost(overlapA, overlapB, parameters);
            case CostFunction::GrainRobust: return computeGrainRobustCost(overlapA, overlapB, parameters);
            case CostFunction::LocalNormalization: return computeLocalNormalizationCost(overlapA, overlapB, parameters);
            case CostFunction::HighlightPenalty: return computeHighlightPenaltyCost(overlapA, overlapB, parameters);
            case CostFunction::CenterPrior: return computeCenterPriorCost(overlapA, overlapB, parameters);
        }
        throw std::invalid_argument("Unknown cost function");
    }

    // Les 7 modes disponibles.
    const std::vector<CostFunction>& getAllCostFunctions() {
        static const std::vector<CostFunction> allCostFunctions = {
            CostFunction::ColorGradient, CostFunction::ColorOnly,          CostFunction::GradientOnly,
            CostFunction::GrainRobust,   CostFunction::LocalNormalization, CostFunction::HighlightPenalty,
            CostFunction::CenterPrior};
        return allCostFunctions;
    }

    // Nom d'un mode dans le notebook.
    std::string getCostFunctionName(CostFunction costFunction) {
        switch (costFunction) {
            case CostFunction::ColorGradient: return "color_gradient";
            case CostFunction::ColorOnly: return "color_only";
            case CostFunction::GradientOnly: return "gradient_only";
            case CostFunction::GrainRobust: return "grain_robust";
            case CostFunction::LocalNormalization: return "local_norm";
            case CostFunction::HighlightPenalty: return "highlight_penalty";
            case CostFunction::CenterPrior: return "center_prior";
        }
        throw std::invalid_argument("Unknown cost function");
    }

    // Conversion inverse : nom du notebook vers le mode.
    CostFunction parseCostFunctionName(const std::string& name) {
        for (CostFunction costFunction : getAllCostFunctions())
            if (getCostFunctionName(costFunction) == name) return costFunction;
        throw std::invalid_argument("Unknown cost function: " + name);
    }

}
