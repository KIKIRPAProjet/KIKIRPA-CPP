#pragma once
#include "core/Step.h"
#include "algorithms/seamline/graphcut/GraphCutSeamFinder.h"
#include <string>
#include <vector>

namespace stitching::steps {

// Entrée du step (utilisée en appel direct ; dans un pipeline, les données viennent du DataStore)
struct GraphCutInput {
    std::vector<std::string> tile_paths;   // chemins vers les tuiles à traiter
    std::vector<TilePositionData> positions;   // tile_id (rLL_cCC), position du coin supérieur gauche dans un repère absolu
    double scale = 1;   // facteur de réduction pour la coupe de graphe (0 < scale <= 1)
};

// Sortie du step
struct GraphCutOutput {
    std::vector<Seamline::TileTerritory> tiles;   // territoire de chaque tuile
};
class GraphCutStep : public Step {
    GraphCutInput input;
    GraphCutOutput output;
public:
    GraphCutStep(const GraphCutInput& input);
    virtual bool execute() override;
    const GraphCutOutput& getOutput() const {
        return output;
    }
};

} // namespace stitching::steps
