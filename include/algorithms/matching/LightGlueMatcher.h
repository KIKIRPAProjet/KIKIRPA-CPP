#pragma once
#include "algorithms/matching/FeatureMatcher.h"

#include <onnxruntime_cxx_api.h>
#include <memory>

namespace stitching::matching {

class LightGlueMatcher : public FeatureMatcher {
private:
    Json params;
    // Permet de ne pas recharger le modèle à chaque paire d'images
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "LightGlue"};
    std::unique_ptr<Ort::Session> session;
public:
    explicit LightGlueMatcher(const Json& params = {});
    bool match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) override;
};

} // namespace stitching::matching
