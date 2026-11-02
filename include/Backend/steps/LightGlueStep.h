#pragma once
#include "algorithms/stitching/FeatureMatcher.h"
#include "core/Step.h"
#include <functional>
#include <memory>

namespace stitching::steps {

struct LightGlueInput {
    std::string modelPath;
    int minMatches = 4;
    float overlap = 0.2f;
};

struct LightGlueOutput {
    std::string key = "matches";
    std::vector<PairMatches> value;
    std::string type = "PairMatches";
};

void to_json(Json& json, const LightGlueOutput& output);

class LightGlueStep : public Step {
public:
    using MatcherFactory = std::function<std::unique_ptr<matching::FeatureMatcher>(const Json&)>;

    // La fabrique optionnelle permet de tester l'orchestration sans modèle ONNX.
    explicit LightGlueStep(const LightGlueInput& input = {}, MatcherFactory factory = {});
    bool execute() override;
    const LightGlueOutput& getOutput() const { return output; }

private:
    MatcherFactory factory;
    std::unique_ptr<matching::FeatureMatcher> matcher;
    std::string loadedModel;
    LightGlueOutput output;
};

} // namespace stitching::steps
