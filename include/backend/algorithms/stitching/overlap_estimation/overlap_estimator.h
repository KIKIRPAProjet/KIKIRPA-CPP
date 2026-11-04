// include/backend/algorithms/stitching/overlap_estimation/overlap_estimator.h
#pragma once

#include "algorithms/stitching/phase_correlation/phase_correlation_types.h"
#include "core/data/algorithm_types.h"

#include <opencv2/core.hpp>

#include <optional>
#include <string>
#include <utility>

namespace OverlapEstimationNs {

    // Première passe, commune à toutes les méthodes d'appariement : position de B par rapport à A au pixel près,
    // mesurée par corrélation de phase sur les bandes de recouvrement nominal (context.overlapFraction), et recouvrement
    // réel qui s'en déduit. Les méthodes d'appariement travaillent ensuite sur la zone commune réelle
    // (computeCommonAreas) : la corrélation de phase y mesure la translation au sous-pixel (deuxième passe), SIFT et
    // LightGlue y cherchent leurs points.
    //
    // Paramètres : ceux de la corrélation de phase (PhaseCorrelationNs::Parameters). Seuls smoothingSigma,
    // minimumPeakRatio, railMargin et minimumValidFraction servent ; upsamplingFactor est ignoré (mesure au pixel près).
    // std::invalid_argument à la construction si une valeur est invalide.
    class OverlapEstimatorClass {

        public:
            explicit OverlapEstimatorClass(const PhaseCorrelationNs::Parameters& initialParameters = {});

            // Position de B dans A au pixel près et recouvrement réel. Vide si la mesure est impossible ou peu fiable
            // (mêmes cas que PhaseCorrelationMatcherClass::match) : la raison est dans getFailureReason().
            std::optional<DataNs::OverlapEstimate> estimate(const cv::Mat& imageA, const cv::Mat& imageB,
                                                            const DataNs::PairContext& context);

            // Pourquoi le dernier estimate() a échoué, avec la correction à apporter quand elle est connue. Vide sinon.
            const std::string& getFailureReason() const { return failureReason; }

            // Rectangles de la zone commune dans A et dans B (même taille), B placée à `translation` arrondie au pixel
            // (coin haut-gauche de B dans le repère de A). Rectangles vides si les tuiles ne se superposent pas.
            static std::pair<cv::Rect, cv::Rect> computeCommonAreas(cv::Size sizeA, cv::Size sizeB, cv::Point2d translation);

        private:
            PhaseCorrelationNs::Parameters parameters;   // vérifiés une seule fois, dans le constructeur
            std::string failureReason;                   // message du dernier échec de estimate()

    };

}
