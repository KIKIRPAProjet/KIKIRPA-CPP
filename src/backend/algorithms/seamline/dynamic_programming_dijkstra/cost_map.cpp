// src/backend/algorithms/seamline/dynamic_programming_dijkstra/cost_map.cpp
#include "algorithms/seamline/dynamic_programming_dijkstra/cost_map.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace DynamicProgrammingDijkstraNs {

    namespace {

        // Copie toutes les valeurs (tous canaux) dans un vecteur de doubles.
        // La conversion écrit directement dans le vecteur : pas de cv::Mat intermédiaire.
        std::vector<double> toVector(const cv::Mat& matrix) {
            const cv::Mat flatMatrix = matrix.reshape(1, 1);
            std::vector<double> values(flatMatrix.total());
            cv::Mat view(1, static_cast<int>(values.size()), CV_64F, values.data());
            flatMatrix.convertTo(view, CV_64F);   // même taille et même type : OpenCV écrit dans `view` sans réallouer
            return values;
        }

        // Équivalent de np.median. Réordonne `values` sur place au lieu d'en faire une copie.
        double computeMedianInPlace(std::vector<double>& values) {
            if (values.empty()) throw std::invalid_argument("median: empty array");
            const size_t middleIndex = values.size() / 2;
            std::nth_element(values.begin(), values.begin() + middleIndex, values.end());
            const double upperValue = values[middleIndex];
            if (values.size() % 2 == 1) return upperValue;
            const double lowerValue = *std::max_element(values.begin(), values.begin() + middleIndex);
            return 0.5 * (lowerValue + upperValue);
        }

        // Équivalent de np.percentile (méthode par défaut : interpolation linéaire).
        double computePercentile(std::vector<double> values, double percentileRank) {
            if (values.empty()) throw std::invalid_argument("percentile: empty array");
            const double rank = percentileRank / 100.0 * double(values.size() - 1);
            const size_t lowerIndex = size_t(std::floor(rank));
            const size_t upperIndex = std::min(lowerIndex + 1, values.size() - 1);
            std::nth_element(values.begin(), values.begin() + lowerIndex, values.end());
            const double lowerValue = values[lowerIndex];
            const double upperValue = (upperIndex == lowerIndex)
                                          ? lowerValue
                                          : *std::min_element(values.begin() + lowerIndex + 1, values.end());
            return lowerValue + (upperValue - lowerValue) * (rank - double(lowerIndex));
        }

        bool isOneOf(const std::string& value, std::initializer_list<const char*> allowedValues) {
            for (const char* allowedValue : allowedValues)
                if (value == allowedValue) return true;
            return false;
        }

        // |Gx| du gris d'une image RGB ou déjà grise. Seul Gx sert au facteur de texture : on ne calcule ni Gy ni la norme.
        cv::Mat computeAbsoluteSobelX(const cv::Mat& image) {
            cv::Mat gradientX;
            cv::Sobel(convertToRawGrey(image), gradientX, CV_64F, 1, 0, 3);   // mêmes réglages que computeGradients()
            cv::absdiff(gradientX, cv::Scalar::all(0), gradientX);            // = cv::abs(gradientX), sur place
            return gradientX;
        }

    }

    Gradients computeGradients(const cv::Mat& grey) {
        CV_Assert(grey.channels() == 1);
        Gradients gradients;
        cv::Sobel(grey, gradients.gradientX, CV_64F, 1, 0, 3);
        cv::Sobel(grey, gradients.gradientY, CV_64F, 0, 1, 3);
        cv::magnitude(gradients.gradientX, gradients.gradientY, gradients.magnitude);   // sqrt(Gx^2 + Gy^2) élément par élément
        return gradients;
    }

    cv::Mat computeGradientMagnitude(const cv::Mat& grey) {
        return computeGradients(grey).magnitude;
    }

    cv::Mat normalizeMinMax(const cv::Mat& values) {
        double lowestValue = 0.0, highestValue = 0.0;
        cv::minMaxLoc(values, &lowestValue, &highestValue);
        if (highestValue > lowestValue) return (values - lowestValue) / (highestValue - lowestValue);
        return cv::Mat::zeros(values.size(), values.type());
    }

    double computeMedianAbsoluteDeviation(const cv::Mat& image) {
        // Un seul vecteur : la médiane le réordonne, ce qui ne change ni la médiane ni les écarts.
        std::vector<double> values = toVector(image);
        const double median = computeMedianInPlace(values);
        for (double& value : values) value = std::abs(value - median);
        return computeMedianInPlace(values);
    }

    cv::Mat normalizeByPercentile(const cv::Mat& image, double percentileRank) {
        const double ceiling = computePercentile(toVector(image), percentileRank);
        // Comme en Python : si ceiling vaut 0, la division produit des NaN.
        cv::Mat clipped = cv::max(image, 0.0);
        clipped = cv::min(clipped, ceiling);
        return clipped / ceiling;
    }

    cv::Mat convertToRawGrey(const cv::Mat& image) {
        CV_Assert(image.channels() == 1 || image.channels() == 3);
        cv::Mat grey;
        if (image.channels() == 1) {               // déjà en gris (ex. IRR) : rien à mélanger
            image.convertTo(grey, CV_32F);
            return grey;
        }
        if (image.depth() == CV_32F) {             // déjà en float : pas de copie intermédiaire
            cv::cvtColor(image, grey, cv::COLOR_RGB2GRAY);
            return grey;
        }
        cv::Mat floatImage;
        image.convertTo(floatImage, CV_32FC3);
        cv::cvtColor(floatImage, grey, cv::COLOR_RGB2GRAY);
        return grey;
    }

    // Mémoire : chaque matrice intermédiaire vit dans un bloc { } ou est libérée par release()
    // dès sa dernière utilisation. Les calculs et leur ordre sont ceux de la version d'origine.
    CostMaps computeCostMaps(const cv::Mat& overlapA, const cv::Mat& overlapB, const CostMapParameters& parameters) {
        if (!isOneOf(parameters.normalization, {"noise", "percentile"}))
            throw std::invalid_argument("normalization must be 'noise' or 'percentile'.");
        if (!isOneOf(parameters.colorNorm, {"manhattan", "euclidian"}))
            throw std::invalid_argument("colorNorm must be 'manhattan' or 'euclidian'.");
        if (!isOneOf(parameters.treatment, {"sigmoid", "linear", "squared", "caped"}))
            throw std::invalid_argument("treatment must be sigmoid, linear, squared or caped");
        // 1 canal (gris, ex. IRR) ou 3 canaux (RGB). En gris, l'écart de couleur se réduit à |A − B|.
        CV_Assert((overlapA.channels() == 1 || overlapA.channels() == 3) && overlapA.channels() == overlapB.channels() &&
                  overlapA.size() == overlapB.size());

        const bool isNoiseNormalization = (parameters.normalization == "noise");
        const int channelCount = overlapA.channels();

        // 1a. Écart de couleur
        // colorDifference = A − B, calculé dans le buffer de A : deux images float au lieu de trois.
        cv::Mat colorDifference;
        overlapA.convertTo(colorDifference, CV_MAKETYPE(CV_32F, channelCount));
        {
            cv::Mat floatOverlapB;
            overlapB.convertTo(floatOverlapB, CV_MAKETYPE(CV_32F, channelCount));
            colorDifference -= floatOverlapB;
        }   // floatOverlapB libéré

        // Un canal à la fois (au lieu de split, qui garde les 3 en mémoire) : norme et MAD.
        cv::Mat colorTerm = cv::Mat::zeros(colorDifference.size(), CV_32F);
        double medianAbsoluteDeviationSum = 0.0;
        {
            cv::Mat channel;
            for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
                cv::extractChannel(colorDifference, channel, channelIndex);
                if (parameters.colorNorm == "manhattan")
                    colorTerm += cv::abs(channel);
                else
                    colorTerm += channel.mul(channel);
                if (isNoiseNormalization) medianAbsoluteDeviationSum += computeMedianAbsoluteDeviation(channel);
            }
        }   // canal libéré
        if (parameters.colorNorm == "manhattan")
            colorTerm /= double(channelCount);
        else
            cv::sqrt(colorTerm, colorTerm);

        // 2. Normalisation de la couleur
        if (isNoiseNormalization) {
            const double medianAbsoluteDeviation = medianAbsoluteDeviationSum / double(channelCount);   // moyenne des MAD par canal
            // Plancher pour ne pas diviser par quelque chose < 1
            const double colorNoise = std::max(medianAbsoluteDeviation * 1.4826, 1.0);
            colorTerm = colorTerm / colorNoise;
        } else {
            colorTerm = normalizeByPercentile(colorTerm, 99);
        }

        // 1b. Écart de gradient + normalisation
        double gradientNoise = 1.0;
        cv::Mat gradientTerm;
        {
            Gradients gradients;
            {
                const cv::Mat greyDifference = convertToRawGrey(colorDifference);
                colorDifference.release();                                   // dernière utilisation de A − B
                gradients = computeGradients(greyDifference);
            }   // greyDifference libéré
            gradientNoise = std::max((computeMedianAbsoluteDeviation(gradients.gradientX) +
                                      computeMedianAbsoluteDeviation(gradients.gradientY)) * 1.4826 / 2.0,
                                     1.0);
            gradients.gradientX.release();                                   // Gx et Gy ne servaient qu'au bruit
            gradients.gradientY.release();
            if (isNoiseNormalization)
                gradientTerm = gradients.magnitude / gradientNoise;
            else
                gradientTerm = normalizeByPercentile(gradients.magnitude, 99);
        }   // norme du gradient libérée

        // 3. Combinaison
        // combined = colorWeight * colorTerm + (1 - colorWeight) * gradientTerm   (en CV_64F, comme NumPy)
        cv::Mat combined;
        {
            cv::Mat weightedColorTerm;
            cv::Mat(colorTerm * parameters.colorWeight).convertTo(weightedColorTerm, CV_64F);
            combined = weightedColorTerm + (1.0 - parameters.colorWeight) * gradientTerm;
        }   // weightedColorTerm libéré

        // 4. Texture : on favorise les hautes fréquences en divisant par Gx de A et Gx de B
        {
            cv::Mat texture = computeAbsoluteSobelX(overlapA);
            cv::min(texture, computeAbsoluteSobelX(overlapB), texture);
            cv::Mat textureFactor;
            cv::divide(gradientNoise, gradientNoise + texture, textureFactor);   // = 1 si les gradients horizontaux sont nuls
            texture.release();
            combined = combined.mul(textureFactor);
        }   // textureFactor libéré

        // 5. Traitement
        cv::Mat treated;
        if (parameters.treatment == "sigmoid" && isNoiseNormalization) {
            cv::Mat exponential;
            cv::exp(-4.0 * (combined - 3.0), exponential);
            combined.release();
            cv::divide(1.0, 1.0 + exponential, treated);
        } else if (parameters.treatment == "squared") {
            treated = combined.mul(combined);
        } else if (parameters.treatment == "caped") {
            treated = cv::max(combined, 0.0);
            treated = cv::min(treated, 5.0);
        } else {
            treated = combined;
        }
        combined.release();

        // 6. Rappel vers le centre
        if (parameters.recall) {
            // Rappel vers le centre : nul sur la moitié centrale, quadratique jusqu'au bord, recallWeight par ligne
            const int width = treated.cols;
            const double halfWidth = std::max((width - 1) / 2.0, 1.0);
            cv::Mat recallRow(1, width, CV_64F);
            for (int column = 0; column < width; ++column) {
                const double distanceToCenter = std::abs(column - (width - 1) / 2.0) / halfWidth;   // 0 au centre, 1 au bord
                const double excess = std::max((distanceToCenter - 0.5) / 0.5, 0.0);
                recallRow.at<double>(0, column) = excess * excess;
            }
            treated = treated + parameters.recallWeight * cv::repeat(recallRow, treated.rows, 1);
        }

        CostMaps maps;
        maps.colorTerm = colorTerm;
        maps.gradientTerm = gradientTerm;
        treated.convertTo(maps.treated, CV_32F);
        return maps;
    }

}
