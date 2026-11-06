// include/backend/algorithms/stitching/lightglue/lightglue_matcher.h
#pragma once

#include "algorithms/stitching/feature_matcher.h"
#include "core/data/algorithm_types.h"

#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/mat.hpp>

#include <memory>

namespace LightGlueNs {

    using Json = nlohmann::json;

    // Appariement par un modèle LightGlue exporté en ONNX (SuperPoint/DISK à deux entrées, ou RACO/ALIKED en batch),
    // sur des bandes de recouvrement déjà découpées. Le SDK ONNX reste privé à ce module.
    //
    // Réglages (JSON du constructeur) : model_path, chemin du modèle ONNX (absolu, ou relatif au répertoire de travail,
    //   à son parent, à la racine du projet ou à build/). std::invalid_argument s'il manque ou si le fichier n'existe pas.
    // Sortie : correspondances de score ≥ 0.9, translation = médiane de pointsInA − pointsInB ; false si aucune.
    class LightGlueMatcherClass : public StitchingNs::FeatureMatcherClass {

        public:
            LightGlueMatcherClass();
            explicit LightGlueMatcherClass(const Json& initialParameters);
            ~LightGlueMatcherClass() override;

            bool match(const cv::Mat& imageA, const cv::Mat& imageB, const DataNs::PairContext& context,
                       DataNs::Matches& matches) override;

        private:
            struct SessionState;
            std::unique_ptr<SessionState> state;

    };

}
