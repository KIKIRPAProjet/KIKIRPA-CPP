// src/backend/algorithms/stitching/phase_correlation/preparation.cpp
#include "algorithms/stitching/phase_correlation/preparation.h"

#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace PhaseCorrelationNs {

    using DataNs::Direction;

    namespace {

        // Seuils de findContentRectangle (part de pixels pleins)
        constexpr double MINIMUM_COLUMN_FILL = 0.1;   // en dessous : colonne presque vide, retirée
        constexpr double MINIMUM_LINE_FILL = 0.5;     // en dessous : ligne / colonne à moitié vide, retirée
        constexpr int MINIMUM_CONTENT_SIDE = 32;      // px : zone commune plus petite = mesure peu fiable
        constexpr double RAIL_MARGIN_TILE_FRACTION = 0.05;   // marge du rail par défaut : part de la tuile dans le sens
                                                             // du déplacement
        constexpr double MINIMUM_RAIL_MARGIN = 48.0;  // px : marge du rail par défaut minimale
        constexpr double SATURATION_FRACTION = 0.998; // gris ≥ cette part du maximum du type : pixel saturé (reflet)
        constexpr int EROSION_RADIUS = 4;             // px rognés autour des pixels vides ou saturés
        constexpr double SECOND_PASS_MARGIN = 4.0;    // px : écart maximal permis à la translation de la première passe

        // Côté opposé (Top ↔ Bottom, Left ↔ Right).
        Direction oppositeDirection(Direction direction) {
            switch (direction) {
                case Direction::Top:    return Direction::Bottom;
                case Direction::Bottom: return Direction::Top;
                case Direction::Left:   return Direction::Right;
                case Direction::Right:  return Direction::Left;
            }
            return direction;
        }

        // Rectangle de la bande de recouvrement d'une tuile de taille `tileSize` sur le côté `side`.
        cv::Rect stripRectangle(cv::Size tileSize, Direction side, double overlapFraction) {
            const int height = tileSize.height, width = tileSize.width;
            const int stripHeight = static_cast<int>(height * overlapFraction);
            const int stripWidth = static_cast<int>(width * overlapFraction);
            switch (side) {
                case Direction::Top:    return {0, 0, width, stripHeight};
                case Direction::Bottom: return {0, height - stripHeight, width, stripHeight};
                case Direction::Left:   return {0, 0, stripWidth, height};
                case Direction::Right:  return {width - stripWidth, 0, stripWidth, height};
            }
            return {};
        }

        // Écart maximal (px) entre la translation globale et le recouvrement nominal : parameters.railMargin si > 0,
        // sinon RAIL_MARGIN_TILE_FRACTION de la tuile dans le sens du déplacement, au moins MINIMUM_RAIL_MARGIN.
        double computeRailMargin(const OrientedPair& pair, const Parameters& parameters) {
            if (parameters.railMargin > 0) return parameters.railMargin;
            // L'imprécision du rail grandit avec le pas, donc avec la taille de la tuile dans le sens du déplacement.
            // Même valeur par défaut que la chaîne Python de référence (Stitching_Python).
            const int tileLength = pair.direction == Direction::Bottom ? pair.imageA.rows : pair.imageA.cols;
            return std::max(RAIL_MARGIN_TILE_FRACTION * tileLength, MINIMUM_RAIL_MARGIN);
        }

        // Masque des pixels à 0 (tous les canaux) reliés au bord de la tuile : la zone hors de l'œuvre.
        cv::Mat computeOutsideMask(const cv::Mat& tile) {
            // Un pixel noir à l'intérieur de la peinture (ombre bouchée à 0) qui ne touche pas le bord reste une donnée.
            // Calculé sur la tuile entière : une zone extérieure peut entrer dans une bande par l'intérieur de la tuile.
            // Limite : les quatre bords servent de départ, y compris ceux partagés avec une tuile voisine (voir la doc).
            cv::Mat emptyMask;
            cv::inRange(tile, cv::Scalar::all(0), cv::Scalar::all(0), emptyMask);   // 255 si tous les canaux valent 0
            const int height = emptyMask.rows, width = emptyMask.cols;

            // Remplissage (connexité 4) depuis chaque pixel vide du bord : les zones reliées au bord passent à 128
            for (int column = 0; column < width; ++column)
                for (int row : {0, height - 1})
                    if (emptyMask.at<uchar>(row, column) == 255)
                        cv::floodFill(emptyMask, cv::Point(column, row), cv::Scalar(128), nullptr, cv::Scalar(), cv::Scalar(), 4);
            for (int row = 0; row < height; ++row)
                for (int column : {0, width - 1})
                    if (emptyMask.at<uchar>(row, column) == 255)
                        cv::floodFill(emptyMask, cv::Point(column, row), cv::Scalar(128), nullptr, cv::Scalar(), cv::Scalar(), 4);
            return emptyMask == 128;
        }

        // Plus grande valeur d'un pixel pour la profondeur OpenCV `depth` (255, 65535… ; 1 pour un flottant).
        double maximumValueOfDepth(int depth) {
            // Les tuiles flottantes sont supposées normalisées entre 0 et 1
            switch (depth) {
                case CV_8U:  return std::numeric_limits<uint8_t>::max();
                case CV_8S:  return std::numeric_limits<int8_t>::max();
                case CV_16U: return std::numeric_limits<uint16_t>::max();
                case CV_16S: return std::numeric_limits<int16_t>::max();
                case CV_32S: return std::numeric_limits<int32_t>::max();
                default:     return 1.0;
            }
        }

        // Gris float64 (moyenne des canaux) ; zone extérieure et pixels saturés → NaN, puis NaN étendu de
        // EROSION_RADIUS px ; zone uniforme → tout NaN.
        cv::Mat convertToGrey(const cv::Mat& tile, const cv::Mat& outsideMask) {
            // Pas de normalisation par percentiles comme dans le Python : la corrélation de phase (phase seule)
            // et la NCC sont insensibles à une transformation affine de l'intensité.
            cv::Mat luminance;
            tile.convertTo(luminance, CV_64F);   // pas de mise à l'échelle : la dynamique 16 bits est conservée
            if (luminance.channels() > 1) {      // tuile couleur : moyenne des canaux
                cv::Mat grey;
                cv::transform(luminance, grey, cv::Mat(1, luminance.channels(), CV_64F, cv::Scalar(1.0 / luminance.channels())));
                luminance = grey;
            }
            luminance.setTo(std::numeric_limits<double>::quiet_NaN(), outsideMask);

            // Pixels saturés (reflets) : l'écrêtage efface la texture, et la tache claire n'est pas au même endroit
            // dans les deux tuiles ; elle attirerait le pic vers un faux décalage.
            luminance.setTo(std::numeric_limits<double>::quiet_NaN(),
                            luminance >= SATURATION_FRACTION * maximumValueOfDepth(tile.depth()));

            // La transition entre une zone retirée et la peinture (quelques px sombres ou très clairs, non retirés)
            // est au même endroit dans les deux tuiles : elle formerait un faux détail commun, de décalage nul.
            // On la rogne. Le bord de l'image n'est pas érodé (valeur de bord par défaut de cv::erode).
            cv::Mat validMask = PreparationClass::computeValidMask(luminance);
            if (cv::countNonZero(validMask) < static_cast<int>(validMask.total())) {
                cv::Mat erodedMask;
                cv::erode(validMask, erodedMask,
                          cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2 * EROSION_RADIUS + 1, 2 * EROSION_RADIUS + 1)));
                luminance.setTo(std::numeric_limits<double>::quiet_NaN(), ~erodedMask);
                validMask = erodedMask;
            }

            // Zone vide ou uniforme : aucune information de position
            double lowestValue = 0, highestValue = 0;
            if (cv::countNonZero(validMask) > 0) cv::minMaxLoc(luminance, &lowestValue, &highestValue, nullptr, nullptr, validMask);
            if (highestValue == lowestValue) luminance.setTo(std::numeric_limits<double>::quiet_NaN());
            return luminance;
        }

        // Rectangle où les deux images ont du contenu ; vide si moins de MINIMUM_CONTENT_SIDE px de côté.
        std::optional<cv::Rect> findContentRectangle(const cv::Mat& imageA, const cv::Mat& imageB) {
            // Le bord d'une zone vide est au même endroit dans les deux bandes : on le coupe,
            // sinon il crée un faux pic à décalage nul.
            cv::Mat filledFraction;
            cv::Mat(PreparationClass::computeValidMask(imageA) & PreparationClass::computeValidMask(imageB))
                .convertTo(filledFraction, CV_64F, 1.0 / 255);
            const int height = filledFraction.rows, width = filledFraction.cols;
            std::vector<int> keptColumns, keptRows;

            // 1. Colonnes presque vides
            cv::Mat columnMeans;
            cv::reduce(filledFraction, columnMeans, 0, cv::REDUCE_AVG);
            for (int column = 0; column < width; ++column)
                if (columnMeans.at<double>(0, column) > MINIMUM_COLUMN_FILL) keptColumns.push_back(column);

            // 2. Lignes à moitié vides (sur les colonnes gardées)
            if (!keptColumns.empty()) {
                for (int row = 0; row < height; ++row) {
                    const double* rowValues = filledFraction.ptr<double>(row);
                    double rowSum = 0;
                    for (int column : keptColumns) rowSum += rowValues[column];
                    if (rowSum / keptColumns.size() > MINIMUM_LINE_FILL) keptRows.push_back(row);
                }
            }

            // 3. Colonnes à moitié vides (sur les lignes gardées)
            if (!keptRows.empty()) {
                std::vector<double> columnSums(width, 0.0);
                for (int row : keptRows) {
                    const double* rowValues = filledFraction.ptr<double>(row);
                    for (int column = 0; column < width; ++column) columnSums[column] += rowValues[column];
                }
                keptColumns.clear();
                for (int column = 0; column < width; ++column)
                    if (columnSums[column] / keptRows.size() > MINIMUM_LINE_FILL) keptColumns.push_back(column);
            }

            if (keptRows.empty() || keptColumns.empty()) return std::nullopt;
            if (keptRows.back() - keptRows.front() < MINIMUM_CONTENT_SIDE ||
                keptColumns.back() - keptColumns.front() < MINIMUM_CONTENT_SIDE)
                return std::nullopt;
            return cv::Rect(keptColumns.front(), keptRows.front(),
                            keptColumns.back() - keptColumns.front() + 1, keptRows.back() - keptRows.front() + 1);
        }

        // Zones `rectangleA` de A et `rectangleB` de B (même taille) en gris ; Strips découpées sur leur zone avec
        // contenu. Vide si moins de parameters.minimumValidFraction pixels exploitables communs ou zone avec contenu
        // trop petite.
        std::optional<Strips> prepareStrips(const OrientedPair& pair, cv::Rect rectangleA, cv::Rect rectangleB,
                                            cv::Point2d origin, double searchMargin, const Parameters& parameters) {
            if (rectangleA.width < 2 || rectangleA.height < 2) return std::nullopt;
            const cv::Mat stripA = convertToGrey(pair.imageA(rectangleA), pair.outsideMaskA(rectangleA));
            const cv::Mat stripB = convertToGrey(pair.imageB(rectangleB), pair.outsideMaskB(rectangleB));

            // Bandes surtout faites de fond ou de reflets : le peu qui reste ne suffit pas à une mesure fiable
            const double validFraction = cv::countNonZero(cv::Mat(PreparationClass::computeValidMask(stripA) &
                                                                  PreparationClass::computeValidMask(stripB))) /
                                         static_cast<double>(stripA.total());
            if (validFraction < parameters.minimumValidFraction) return std::nullopt;

            // Même découpe dans les deux bandes : le décalage résiduel mesuré n'en est pas modifié
            const auto contentRectangle = findContentRectangle(stripA, stripB);
            if (!contentRectangle) return std::nullopt;

            Strips strips;
            strips.stripA = stripA(*contentRectangle);
            strips.stripB = stripB(*contentRectangle);
            strips.origin = origin;
            strips.searchMargin = searchMargin;
            return strips;
        }

    }

    void PreparationClass::validateParameters(const Parameters& parameters) {
        // Vérifié une fois à la construction : une valeur hors domaine est une erreur de configuration,
        // pas un échec de mesure, d'où l'exception plutôt qu'un false dans match()
        if (parameters.maximumPointCount < 0 || parameters.patchSize < 8 || parameters.smoothingSigma < 0 ||
            parameters.localSmoothingSigma < 0 || parameters.maximumLocalDeviation < 0 || parameters.minimumPointSpacing < 0 ||
            parameters.railMargin < 0 || parameters.minimumValidFraction < 0 || parameters.minimumValidFraction > 1 ||
            parameters.upsamplingFactor < 1)
            throw std::invalid_argument("phase-correlation: invalid parameter (nbPoints >= 0, fenetre >= 8, "
                                        "facteurDft >= 1, fractionValideMin in [0, 1], "
                                        "lissage, lissageLocal, ecartMax, espacementMin, marge >= 0)");
    }

    std::optional<OrientedPair> PreparationClass::orientPair(const cv::Mat& imageA, const cv::Mat& imageB,
                                                             const DataNs::PairContext& context) {
        if (imageA.empty() || imageB.empty() || imageA.size() != imageB.size()) return std::nullopt;
        if (context.overlapFraction <= 0.f || context.overlapFraction > 1.f) return std::nullopt;

        // B en haut ou à gauche : on se ramène au cas bas / droite en échangeant A et B
        const bool isSwapped = context.direction == Direction::Top || context.direction == Direction::Left;
        OrientedPair pair;
        pair.imageA = isSwapped ? imageB : imageA;
        pair.imageB = isSwapped ? imageA : imageB;
        pair.outsideMaskA = computeOutsideMask(pair.imageA);
        pair.outsideMaskB = computeOutsideMask(pair.imageB);
        pair.direction = isSwapped ? oppositeDirection(context.direction) : context.direction;
        pair.isSwapped = isSwapped;
        return pair;
    }

    cv::Point2d PreparationClass::orientTranslation(const OrientedPair& pair, cv::Point2d translation) {
        // Échanger A et B revient à placer A dans le repère de B : la translation change de signe
        return pair.isSwapped ? -translation : translation;
    }

    std::optional<Strips> PreparationClass::extractNominalStrips(const OrientedPair& pair, double overlapFraction,
                                                                 const Parameters& parameters) {
        // On corrèle seulement les deux bandes censées se recouvrir (recouvrement nominal du rail),
        // pas les tuiles entières : ailleurs, le contenu est différent et brouillerait le pic.
        const cv::Rect rectangleA = stripRectangle(pair.imageA.size(), pair.direction, overlapFraction);
        const cv::Rect rectangleB = stripRectangle(pair.imageB.size(), oppositeDirection(pair.direction), overlapFraction);
        const cv::Point2d origin = pair.direction == Direction::Bottom ? cv::Point2d(0, pair.imageA.rows - rectangleA.height)
                                                                       : cv::Point2d(pair.imageA.cols - rectangleA.width, 0);
        // Les bandes sont prises au recouvrement nominal : le décalage résiduel attendu est nul,
        // et la marge du rail le borne directement
        return prepareStrips(pair, rectangleA, rectangleB, origin, computeRailMargin(pair, parameters), parameters);
    }

    std::optional<Strips> PreparationClass::extractCommonArea(const OrientedPair& pair, cv::Point2d translation,
                                                              const Parameters& parameters) {
        // Les deux images montrent la même partie de la peinture, à l'erreur de la première passe près : tout leur
        // contenu se correspond. Rien n'est perdu quand le recouvrement réel dépasse le nominal, et aucun contenu
        // sans correspondance n'affaiblit le pic quand il est plus petit.
        const auto [rectangleA, rectangleB] = OverlapEstimationNs::OverlapEstimatorClass::computeCommonAreas(
            pair.imageA.size(), pair.imageB.size(), translation);
        // Le décalage résiduel est mesuré autour de la translation arrondie : il est de l'ordre du pixel
        return prepareStrips(pair, rectangleA, rectangleB, cv::Point2d(rectangleA.tl() - rectangleB.tl()), SECOND_PASS_MARGIN,
                             parameters);
    }

    std::optional<Overlap> PreparationClass::extractOverlap(const OrientedPair& pair, const Measurement& measurement,
                                                            const Parameters& parameters) {
        // Parties de A et de B qui se superposent quand B est placée à la translation globale arrondie
        const auto [areaA, areaB] = OverlapEstimationNs::OverlapEstimatorClass::computeCommonAreas(
            pair.imageA.size(), pair.imageB.size(), {measurement.translationX, measurement.translationY});

        // Un patch centré sur un candidat, puis décalé de la correction locale, doit rester dans la zone
        const int borderMargin = parameters.patchSize / 2 + static_cast<int>(std::ceil(parameters.maximumLocalDeviation)) + 1;
        if (areaA.height < parameters.patchSize + 2 * borderMargin || areaA.width < parameters.patchSize + 2 * borderMargin)
            return std::nullopt;

        Overlap overlap;
        overlap.regionA = convertToGrey(pair.imageA(areaA), pair.outsideMaskA(areaA));
        overlap.regionB = convertToGrey(pair.imageB(areaB), pair.outsideMaskB(areaB));
        overlap.originInA = areaA.tl();
        overlap.roundedTranslation = areaA.tl() - areaB.tl();
        overlap.borderMargin = borderMargin;
        return overlap;
    }

    cv::Mat PreparationClass::computeValidMask(const cv::Mat& image) {
        return image == image;   // NaN != NaN
    }

    cv::Mat PreparationClass::fillEmptyPixels(const cv::Mat& image) {
        const cv::Mat validMask = computeValidMask(image);
        cv::Mat filledImage = image.clone();
        filledImage.setTo(cv::mean(image, validMask)[0], ~validMask);
        return filledImage;
    }

}
