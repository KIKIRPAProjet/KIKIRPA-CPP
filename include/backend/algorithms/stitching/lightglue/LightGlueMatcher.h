#pragma once
#include "algorithms/legacy_type_aliases.h"
#include "algorithms/stitching/feature_matcher.h"

#include <onnxruntime_cxx_api.h>
#include <memory>
#include <string>

namespace stitching::matching {

class LightGlueMatcher : public StitchingNs::FeatureMatcherClass {
private:
    Json params;
    // Permet de ne pas recharger le modèle à chaque paire d'images
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "LightGlue"};
    std::unique_ptr<Ort::Session> session;

    // Prépare une image au format attendu par le modèle ONNX.
    cv::Mat preprocessImage(const cv::Mat& image, const std::string& imageName, float& scaleX, float& scaleY, bool grayscale = false) const;

public:
    explicit LightGlueMatcher(const Json& params = {});
    bool match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) override;
};

} // namespace stitching::matching
