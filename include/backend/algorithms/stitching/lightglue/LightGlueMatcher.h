// include/backend/algorithms/stitching/lightglue/LightGlueMatcher.h
#pragma once

#include "algorithms/stitching/FeatureMatcher.h"
#include "core/data/algorithm_types.h"

#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/mat.hpp>

#include <memory>

namespace stitching::matching {

    // Le SDK ONNX reste privé : ce header est utilisable même si la dépendance est absente.
    class LightGlueMatcherClass : public FeatureMatcher {

        public:
            LightGlueMatcherClass();
            explicit LightGlueMatcherClass(const nlohmann::json& parameters);
            ~LightGlueMatcherClass() override;
            bool match(const cv::Mat& imageA, const cv::Mat& imageB,
                       const DataNs::PairContext& context, DataNs::Matches& matches) override;

        private:
            cv::Mat preprocessImage(const cv::Mat& image, float& scaleX,
                                    float& scaleY, bool grayscale) const;
            struct SessionState;
            std::unique_ptr<SessionState> state;
    };

    // Compatibilité avec les algorithmes pas encore migrés vers le suffixe Class.
    using LightGlueMatcher = LightGlueMatcherClass;

}
