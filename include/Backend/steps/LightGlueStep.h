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

struct MatchedTile {
    size_t index = 0; // index dans le vecteur Tiles d'origine
    std::filesystem::path path;
    int col = 0, row = 0;
};

struct TilePairMatches {
    MatchedTile a, b;
    Direction direction = Direction::Right;
    Matches matches; // coordonnées dans les images complètes ; aucune transformation RANSAC
};

using PairMatches = std::vector<TilePairMatches>;

class LightGlueStep : public Step {
public:
    using MatcherFactory = std::function<std::unique_ptr<matching::FeatureMatcher>(const Json&)>;

    // La fabrique optionnelle permet de tester l'orchestration sans modèle ONNX.
    explicit LightGlueStep(const LightGlueInput& input = {}, MatcherFactory factory = {});
    bool execute() override;
    const PairMatches& getOutput() const { return output; }

private:
    MatcherFactory factory;
    std::unique_ptr<matching::FeatureMatcher> matcher;
    std::string loadedModel;
    PairMatches output;
};

} // namespace stitching::steps
