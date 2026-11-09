// src/backend/algorithms/stitching/phase_correlation/algorithm.cpp
#include "algorithms/stitching/phase_correlation/algorithm.h"

#include "algorithms/stitching/phase_correlation/preparation.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#if __has_include(<opencv2/features.hpp>)
#include <opencv2/features.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <limits>

namespace PhaseCorrelationNs {

    namespace {

        constexpr double PI = 3.14159265358979323846;
        constexpr double EPSILON = 1e-12;                   // évite la division par 0 dans le spectre croisé
        constexpr int PEAK_RADIUS = 10;                     // px : voisinage du pic exclu pour chercher le second
        constexpr double CORNER_QUALITY = 0.01;             // goodFeaturesToTrack : fraction du meilleur coin
        constexpr int CANDIDATES_PER_POINT = 3;             // candidats testés pour chaque point demandé
        constexpr double MINIMUM_VALID_PATCH_FRACTION = 0.5;   // part minimale de pixels non vides dans un patch
        constexpr double UPSAMPLED_GRID_EXTENT = 1.5;       // px : côté de la grille suréchantillonnée autour du pic entier
        constexpr double NO_MARGIN = std::numeric_limits<double>::infinity();   // pic cherché partout

        // Indice frequencyIndex ramené dans ]-sampleCount/2, sampleCount/2] (décalage signé, comme
        // np.fft.fftfreq(n) · n).
        int signedShift(int frequencyIndex, int sampleCount) {
            // La transformée de Fourier est périodique : un indice au-delà de la moitié est un décalage négatif
            return frequencyIndex <= (sampleCount - 1) / 2 ? frequencyIndex : frequencyIndex - sampleCount;
        }

        // Fréquence de l'indice frequencyIndex pour sampleCount échantillons (np.fft.fftfreq).
        double frequencyOf(int frequencyIndex, int sampleCount) {
            return signedShift(frequencyIndex, sampleCount) / static_cast<double>(sampleCount);
        }

        // Spectre d'une image : vides remplis, moyenne retirée, fenêtre de Hann, DFT complexe.
        cv::Mat computeSpectrum(const cv::Mat& image, const cv::Mat& hannWindow) {
            const cv::Mat filledImage = PreparationClass::fillEmptyPixels(image);
            cv::Mat spectrum;
            cv::dft((filledImage - cv::mean(filledImage)[0]).mul(hannWindow), spectrum, cv::DFT_COMPLEX_OUTPUT);
            return spectrum;
        }

        // spectrumA · conj(spectrumB) / |spectrumA · conj(spectrumB)| : seule la phase (donc la position) est conservée.
        cv::Mat normalizeCrossSpectrum(const cv::Mat& spectrumA, const cv::Mat& spectrumB) {
            cv::Mat crossSpectrum;
            cv::mulSpectrums(spectrumA, spectrumB, crossSpectrum, 0, /*conjB=*/true);
            for (int row = 0; row < crossSpectrum.rows; ++row) {
                auto* rowValues = crossSpectrum.ptr<cv::Vec2d>(row);
                for (int column = 0; column < crossSpectrum.cols; ++column)
                    rowValues[column] /= std::hypot(rowValues[column][0], rowValues[column][1]) + EPSILON;
            }
            return crossSpectrum;
        }

        // Spectre croisé normalisé de deux images de même taille, fenêtre de Hann appliquée.
        cv::Mat computeCrossSpectrum(const cv::Mat& imageA, const cv::Mat& imageB) {
            // Fenêtre de Hann : sans elle, les bords de l'image créent un faux pic en (0, 0)
            cv::Mat hannWindow;
            cv::createHanningWindow(hannWindow, imageA.size(), CV_64F);
            return normalizeCrossSpectrum(computeSpectrum(imageA, hannWindow), computeSpectrum(imageB, hannWindow));
        }

        // exp(-2π²σ²(u² + v²)) : multiplier le spectre par ce filtre floute la surface (σ = smoothingSigma px).
        // Deux canaux identiques, pour multiplier directement un spectre complexe.
        cv::Mat buildGaussianFilter(cv::Size size, double smoothingSigma) {
            cv::Mat gaussianFilter(size, CV_64FC2);
            const double exponentFactor = -2 * PI * PI * smoothingSigma * smoothingSigma;
            for (int row = 0; row < size.height; ++row) {
                auto* rowValues = gaussianFilter.ptr<cv::Vec2d>(row);
                const double frequencyY = frequencyOf(row, size.height);
                for (int column = 0; column < size.width; ++column) {
                    const double frequencyX = frequencyOf(column, size.width);
                    const double attenuation = std::exp(exponentFactor * (frequencyY * frequencyY + frequencyX * frequencyX));
                    rowValues[column] = cv::Vec2d(attenuation, attenuation);
                }
            }
            return gaussianFilter;
        }

        // DFT inverse normalisée, partie réelle.
        cv::Mat inverseRealPart(const cv::Mat& spectrum) {
            cv::Mat complexImage, realImage;
            cv::idft(spectrum, complexImage, cv::DFT_SCALE);
            cv::extractChannel(complexImage, realImage, 0);
            return realImage;
        }

        // Surface de corrélation lissée par un flou gaussien de smoothingSigma px (non centrée : décalage nul en (0, 0)).
        cv::Mat computeSmoothedSurface(const cv::Mat& crossSpectrum, double smoothingSigma) {
            // La surface brute donne le même poids à toutes les fréquences. Le grain du capteur et les motifs
            // fixes de l'acquisition (hautes fréquences) y créent de faux pics fins, parfois plus hauts que le vrai.
            // Lisser la surface revient à réduire le poids des hautes fréquences : le vrai pic, porté aussi
            // par les basses fréquences, ressort.
            return inverseRealPart(crossSpectrum.mul(buildGaussianFilter(crossSpectrum.size(), smoothingSigma)));
        }

        // Masque 8 bits (taille de la surface) des décalages |dx|, |dy| ≤ searchMargin ; vide si searchMargin = NO_MARGIN.
        cv::Mat buildAllowedMask(cv::Size size, double searchMargin) {
            if (!std::isfinite(searchMargin)) return {};
            // La surface n'est pas centrée : l'indice k correspond au décalage signé signedShift(k, n)
            cv::Mat allowedMask(size, CV_8U);
            for (int row = 0; row < size.height; ++row) {
                auto* rowValues = allowedMask.ptr<uchar>(row);
                const bool isRowInMargin = std::abs(signedShift(row, size.height)) <= searchMargin;
                for (int column = 0; column < size.width; ++column)
                    rowValues[column] = isRowInMargin && std::abs(signedShift(column, size.width)) <= searchMargin ? 255 : 0;
            }
            return allowedMask;
        }

        // Indices (x, y) du maximum de la surface parmi les décalages autorisés (masque vide = partout).
        cv::Point locatePeak(const cv::Mat& surface, const cv::Mat& allowedMask) {
            cv::Point peakPixel;
            cv::minMaxLoc(surface, nullptr, nullptr, nullptr, &peakPixel, allowedMask);
            return peakPixel;
        }

        // Noyau complexe (gridSize × sampleCount) : exp(2iπ · k · s_j / n), k fréquence signée, s_j = gridStart + j · step.
        // Multiplier un spectre par ce noyau évalue sa transformée inverse aux décalages s_j le long d'un axe.
        cv::Mat buildDftKernel(int sampleCount, double gridStart, double gridStep, int gridSize) {
            // Fréquences signées : pour un décalage non entier, c'est l'interpolation de Fourier (la même que np.fft)
            cv::Mat kernel(gridSize, sampleCount, CV_64FC2);
            for (int gridIndex = 0; gridIndex < gridSize; ++gridIndex) {
                auto* rowValues = kernel.ptr<cv::Vec2d>(gridIndex);
                const double shift = gridStart + gridIndex * gridStep;
                for (int frequencyIndex = 0; frequencyIndex < sampleCount; ++frequencyIndex) {
                    const double angle = 2 * PI * signedShift(frequencyIndex, sampleCount) * shift / sampleCount;
                    rowValues[frequencyIndex] = cv::Vec2d(std::cos(angle), std::sin(angle));
                }
            }
            return kernel;
        }

        // Décalage (dx, dy) sous-pixel du maximum de la surface brute, cherché à ±UPSAMPLED_GRID_EXTENT/2 px autour du
        // pic entier par DFT suréchantillonnée du spectre croisé (Guizar-Sicairos 2008), au pas de 1/upsamplingFactor px.
        cv::Point2d refinePeak(const cv::Mat& crossSpectrum, cv::Point2d integerPeak, int upsamplingFactor) {
            // Suréchantillonner toute la surface (zéro-padding ×facteur) serait hors de portée sur des bandes de plusieurs
            // milliers de px. On évalue exactement la transformée inverse sur une petite grille autour du pic entier, par
            // deux produits matriciels : grille = Ky · croisé · Kxᵀ, coût proportionnel à (taille de la grille) × pixels.
            const int gridSize = static_cast<int>(std::ceil(UPSAMPLED_GRID_EXTENT * upsamplingFactor));
            const double gridStep = 1.0 / upsamplingFactor;
            const double gridStartX = integerPeak.x - (gridSize / 2) * gridStep;
            const double gridStartY = integerPeak.y - (gridSize / 2) * gridStep;

            cv::Mat rowProducts, grid;
            cv::gemm(buildDftKernel(crossSpectrum.rows, gridStartY, gridStep, gridSize), crossSpectrum, 1.0, cv::noArray(), 0.0,
                     rowProducts);
            cv::gemm(rowProducts, buildDftKernel(crossSpectrum.cols, gridStartX, gridStep, gridSize).t(), 1.0, cv::noArray(), 0.0,
                     grid);

            // Le facteur 1/N de la transformée inverse ne change pas la position du maximum
            cv::Mat realGrid;
            cv::extractChannel(grid, realGrid, 0);
            cv::Point bestPixel;
            cv::minMaxLoc(realGrid, nullptr, nullptr, nullptr, &bestPixel);
            return {gridStartX + bestPixel.x * gridStep, gridStartY + bestPixel.y * gridStep};
        }

        // Translation (shiftY, shiftX) telle que a(y, x) ≈ b(y - shiftY, x - shiftX), pour deux images de même taille.
        // Le pic est cherché parmi les décalages |dx|, |dy| ≤ searchMargin (NO_MARGIN : partout), précision
        // 1/upsamplingFactor px (upsamplingFactor = 1 : pic entier de la surface lissée, sans raffinement).
        Correlation correlatePhase(const cv::Mat& imageA, const cv::Mat& imageB, double smoothingSigma, double searchMargin,
                                   int upsamplingFactor) {
            const cv::Mat crossSpectrum = computeCrossSpectrum(imageA, imageB);
            const cv::Mat rawSurface = inverseRealPart(crossSpectrum);
            const cv::Mat smoothedSurface = computeSmoothedSurface(crossSpectrum, smoothingSigma);

            // Le pic entier est cherché sur la surface lissée : il est plus large mais reste symétrique autour du vrai
            // décalage. Sur la surface brute, quand le pic est faible, le pixel le plus haut est parfois un grain
            // de bruit voisin (erreur de 1 à 2 px).
            // Seulement dans la marge du rail : un pic plus haut au-delà ne peut pas être le bon (motif répétitif,
            // fond uni), le rail ne peut pas s'être trompé d'autant.
            const cv::Mat allowedMask = buildAllowedMask(imageA.size(), searchMargin);
            const cv::Point peakPixel = locatePeak(smoothedSurface, allowedMask);
            const cv::Point2d integerShift(signedShift(peakPixel.x, imageA.cols), signedShift(peakPixel.y, imageA.rows));

            // La fraction de pixel est calculée sur le spectre brut : le lissage déplace légèrement le sommet du pic,
            // et une parabole sur trois valeurs entières est tirée vers l'entier le plus proche.
            // Au pixel près (première passe), on garde le pic de la surface lissée : celui de la surface brute est
            // parfois un grain de bruit voisin.
            const cv::Point2d shift = upsamplingFactor > 1 ? refinePeak(crossSpectrum, integerShift, upsamplingFactor)
                                                           : integerShift;
            return Correlation{shift.y, shift.x, rawSurface.at<double>(peakPixel), smoothedSurface, allowedMask};
        }

        // Hauteur du meilleur pic / celle du second (hors d'un carré de ±peakRadius px autour du premier),
        // tous deux cherchés dans la zone autorisée de la corrélation.
        double computePeakRatio(const Correlation& correlation, int peakRadius) {
            // Proche de 1 : deux positions se valent, la mesure est ambiguë. Nettement au-dessus de 1 : pic franc.
            const cv::Mat& surface = correlation.surface;
            const int height = surface.rows, width = surface.cols;
            double peakHeight;
            cv::Point peakPixel;
            cv::minMaxLoc(surface, nullptr, &peakHeight, nullptr, &peakPixel, correlation.allowedMask);

            // Le second pic est cherché lui aussi dans la marge : un pic hors marge ne rend pas la mesure ambiguë,
            // puisqu'il ne peut pas être le bon.
            cv::Mat maskedSurface = surface.clone();
            if (!correlation.allowedMask.empty())
                maskedSurface.setTo(-std::numeric_limits<double>::infinity(), ~correlation.allowedMask);

            // La surface est périodique : un pic collé à un bord déborde de l'autre côté.
            // Même voisinage que le Python (pic ramené au centre, carré tronqué aux bords), en indices modulo.
            const int centerY = height / 2, centerX = width / 2;
            for (int offsetY = std::max(-peakRadius, -centerY); offsetY <= std::min(peakRadius, height - 1 - centerY); ++offsetY)
                for (int offsetX = std::max(-peakRadius, -centerX); offsetX <= std::min(peakRadius, width - 1 - centerX); ++offsetX)
                    maskedSurface.at<double>((peakPixel.y + offsetY + height) % height, (peakPixel.x + offsetX + width) % width) =
                        -std::numeric_limits<double>::infinity();

            double secondPeakHeight;
            cv::minMaxLoc(maskedSurface, nullptr, &secondPeakHeight);
            return secondPeakHeight <= 0 ? std::numeric_limits<double>::infinity() : peakHeight / secondPeakHeight;
        }

        // Coins texturés et espacés, assez loin des bords pour que patch + correction restent dans la zone.
        std::vector<cv::Point2f> findCandidates(const Overlap& overlap, const Parameters& parameters) {
            if (parameters.maximumPointCount <= 0) return {};   // goodFeaturesToTrack interprète 0 comme « sans limite »
            cv::Mat candidateMask = PreparationClass::computeValidMask(overlap.regionA);
            candidateMask.rowRange(0, overlap.borderMargin).setTo(0);
            candidateMask.rowRange(candidateMask.rows - overlap.borderMargin, candidateMask.rows).setTo(0);
            candidateMask.colRange(0, overlap.borderMargin).setTo(0);
            candidateMask.colRange(candidateMask.cols - overlap.borderMargin, candidateMask.cols).setTo(0);
            if (cv::countNonZero(candidateMask) == 0) return {};

            cv::Mat image;
            PreparationClass::fillEmptyPixels(overlap.regionA).convertTo(image, CV_32F);
            std::vector<cv::Point2f> corners;
            cv::goodFeaturesToTrack(image, corners, CANDIDATES_PER_POINT * parameters.maximumPointCount, CORNER_QUALITY,
                                    parameters.minimumPointSpacing, candidateMask);
            return corners;
        }

        // Corrélation normalisée entre deux patchs de même taille, ramenée dans [0, 1].
        float computeNormalizedCrossCorrelation(const cv::Mat& patchA, const cv::Mat& patchB) {
            cv::Mat floatPatchA, floatPatchB, matchResult;
            PreparationClass::fillEmptyPixels(patchA).convertTo(floatPatchA, CV_32F);
            PreparationClass::fillEmptyPixels(patchB).convertTo(floatPatchB, CV_32F);
            cv::matchTemplate(floatPatchA, floatPatchB, matchResult, cv::TM_CCOEFF_NORMED);
            const float correlationValue = matchResult.at<float>(0, 0);
            // Patch uniforme : NCC indéfinie
            return std::isfinite(correlationValue) ? std::clamp(correlationValue, 0.f, 1.f) : 0.f;
        }

        // Translation locale autour d'un candidat ; vide si le patch est trop vide, incohérent
        // avec la translation globale ou ambigu.
        std::optional<Correspondence> measurePoint(const Overlap& overlap, cv::Point2f center,
                                                   const Measurement& globalMeasurement, const Parameters& parameters) {
            const int patchSize = parameters.patchSize;
            const cv::Rect patchRectangle(static_cast<int>(center.x) - patchSize / 2, static_cast<int>(center.y) - patchSize / 2,
                                          patchSize, patchSize);
            const cv::Mat patchA = overlap.regionA(patchRectangle), patchB = overlap.regionB(patchRectangle);
            const int minimumValidCount = static_cast<int>(MINIMUM_VALID_PATCH_FRACTION * patchSize * patchSize);
            if (cv::countNonZero(PreparationClass::computeValidMask(patchA)) < minimumValidCount ||
                cv::countNonZero(PreparationClass::computeValidMask(patchB)) < minimumValidCount) return std::nullopt;

            // Résidu local par rapport à la translation arrondie : patchA(y, x) ≈ patchB(y - dy, x - dx).
            // Pic cherché partout, sans marge : limiter la recherche à ±maximumLocalDeviation viderait le test du second
            // pic (tout le voisinage serait masqué) ; un pic local qui s'écarte de la globale est rejeté juste après.
            const Correlation localCorrelation = correlatePhase(patchA, patchB, parameters.localSmoothingSigma, NO_MARGIN,
                                                                parameters.upsamplingFactor);
            const cv::Point2d localTranslation(overlap.roundedTranslation.x + localCorrelation.shiftX,
                                               overlap.roundedTranslation.y + localCorrelation.shiftY);
            if (std::abs(localTranslation.x - globalMeasurement.translationX) > parameters.maximumLocalDeviation ||
                std::abs(localTranslation.y - globalMeasurement.translationY) > parameters.maximumLocalDeviation)
                return std::nullopt;
            if (computePeakRatio(localCorrelation, PEAK_RADIUS) < parameters.minimumPeakRatio) return std::nullopt;

            // Confiance mesurée sur le patch de B recalé au pixel près
            const cv::Rect shiftedPatchRectangle = patchRectangle - cv::Point(static_cast<int>(std::lround(localCorrelation.shiftX)),
                                                                              static_cast<int>(std::lround(localCorrelation.shiftY)));
            return Correspondence{center, localTranslation,
                                  computeNormalizedCrossCorrelation(patchA, overlap.regionB(shiftedPatchRectangle))};
        }

    }

    std::optional<Measurement> AlgorithmClass::measureGlobalTranslation(const Strips& strips, const Parameters& parameters) {
        const Correlation correlation = correlatePhase(strips.stripA, strips.stripB, parameters.smoothingSigma, strips.searchMargin,
                                                       parameters.upsamplingFactor);
        const double peakRatio = computePeakRatio(correlation, PEAK_RADIUS);
        if (peakRatio < parameters.minimumPeakRatio) return std::nullopt;   // deux positions se valent : mesure ambiguë
        return Measurement{strips.origin.x + correlation.shiftX, strips.origin.y + correlation.shiftY, peakRatio};
    }

    std::optional<Measurement> AlgorithmClass::refineGlobalTranslation(const Strips& commonArea, const Parameters& parameters) {
        const Correlation correlation = correlatePhase(commonArea.stripA, commonArea.stripB, parameters.smoothingSigma,
                                                       commonArea.searchMargin, parameters.upsamplingFactor);
        // Pic entier sur le bord de la marge : le vrai maximum est sans doute au-delà, la première passe s'est trompée
        // de plus que la marge. Mieux vaut échouer que renvoyer une translation tronquée au bord. Le test porte sur
        // le pic entier : le raffinement peut ramener un pic de bord jusqu'à 0,75 px vers l'intérieur.
        const cv::Point peakPixel = locatePeak(correlation.surface, correlation.allowedMask);
        const int marginBorder = static_cast<int>(std::floor(commonArea.searchMargin));
        if (std::abs(signedShift(peakPixel.x, correlation.surface.cols)) >= marginBorder ||
            std::abs(signedShift(peakPixel.y, correlation.surface.rows)) >= marginBorder)
            return std::nullopt;
        // Le second pic n'est pas contrôlé : la marge (quelques px) est plus petite que le voisinage exclu autour
        // du pic, le test passerait toujours. L'ambiguïté a été contrôlée par la première passe.
        return Measurement{commonArea.origin.x + correlation.shiftX, commonArea.origin.y + correlation.shiftY,
                           std::numeric_limits<double>::quiet_NaN()};
    }

    Diagnostic AlgorithmClass::diagnoseGlobalTranslation(const Strips& strips, const Parameters& parameters) {
        // Même surface que measureGlobalTranslation, sans sous-pixel : seul l'endroit du pic compte pour expliquer
        // l'échec. Le surcoût (une corrélation de plus) n'est payé que lorsque la mesure a déjà échoué.
        const cv::Mat smoothedSurface = computeSmoothedSurface(computeCrossSpectrum(strips.stripA, strips.stripB),
                                                               parameters.smoothingSigma);
        const cv::Mat marginMask = buildAllowedMask(smoothedSurface.size(), strips.searchMargin);
        const cv::Point peakPixel = locatePeak(smoothedSurface, cv::Mat());
        const cv::Point2d unconstrainedPeak(signedShift(peakPixel.x, smoothedSurface.cols),
                                            signedShift(peakPixel.y, smoothedSurface.rows));

        Diagnostic diagnostic;
        diagnostic.peakRatioInMargin = computePeakRatio(Correlation{0, 0, 0, smoothedSurface, marginMask}, PEAK_RADIUS);
        diagnostic.unconstrainedPeak = unconstrainedPeak;
        diagnostic.unconstrainedPeakRatio = computePeakRatio(
            Correlation{unconstrainedPeak.y, unconstrainedPeak.x, 0, smoothedSurface, cv::Mat()}, PEAK_RADIUS);
        diagnostic.isOutsideMargin = std::max(std::abs(unconstrainedPeak.x), std::abs(unconstrainedPeak.y)) > strips.searchMargin;
        return diagnostic;
    }

    std::vector<Correspondence> AlgorithmClass::measureLocalPoints(const Overlap& overlap, const Measurement& globalMeasurement,
                                                                   const Parameters& parameters) {
        std::vector<Correspondence> points;
        for (const cv::Point2f& center : findCandidates(overlap, parameters))
            if (const auto point = measurePoint(overlap, center, globalMeasurement, parameters)) points.push_back(*point);
        return points;
    }

}
