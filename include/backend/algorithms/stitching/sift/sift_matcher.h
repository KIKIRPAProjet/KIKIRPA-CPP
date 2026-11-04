// include/backend/algorithms/stitching/sift/sift_matcher.h
#pragma once

#include "algorithms/stitching/feature_matcher.h"
#include "core/data/algorithm_types.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

namespace SiftNs {

    using Json = nlohmann::json;

    // Appariement par SIFT (OpenCV) + filtre de Lowe, sur des bandes de recouvrement déjà découpées.
    //
    // Réglages, lus dans le JSON du constructeur (valeur par défaut si absent) :
    //   max_features (500), lowe_ratio (0.75), contrast_threshold (0.01), clahe_clip_limit (2.0 ; 0 = sans CLAHE).
    //   Exemple : {"max_features": 800, "lowe_ratio": 0.7}. std::invalid_argument à l'appel de match() si une
    //   valeur est hors domaine.
    //
    // Sortie : matches.pointsInA[i] ↔ matches.pointsInB[i] (au moins 4 paires, sinon false et listes vides) ;
    //   confidenceScores[i] = 1 − (distance du meilleur descripteur / distance du suivant), entre 0 et 1 ;
    //   (translationX, translationY) = médianes de pointsInA − pointsInB.
    class SiftMatcherClass : public StitchingNs::FeatureMatcherClass {

        public:
            explicit SiftMatcherClass(const Json& initialParameters = {});

            bool match(const cv::Mat& imageA, const cv::Mat& imageB, const DataNs::PairContext& context,
                       DataNs::Matches& matches) override;

        private:
            Json parameters;

    };

}
