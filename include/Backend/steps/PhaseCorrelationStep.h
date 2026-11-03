#pragma once
#include "core/Step.h"
#include "algorithms/stitching/correlation_phase/CorrelationPhase.h"
#include <vector>

namespace stitching::steps {

// Entrée du step = ses options
struct PhaseCorrelationInput {
    float overlap = 0.2f;   // recouvrement nominal, fraction de la tuile
    matching::CorrelationPhase::Parametres parametres;
};

// Sortie du step = sa signature
struct PhaseCorrelationOutput {
    std::vector<PairMatches> pairs;
};

// Translation au sous-pixel entre chaque paire de tuiles voisines (droite, dessous), par corrélation de phase.
// Une paire qui échoue est ignorée (raison dans exportData()["failures"]) ; le step échoue si aucune ne réussit.
// name : "phase-correlation"
// options : overlap (float), nbPoints (int), fenetre (int), seuilRapport (float), marge (float), facteurDft (int)
// pipelineOptions : { "type": "Tiles", "key": "tiles" }
// signature : { "type": "PairMatches", "key": "matches" }
class PhaseCorrelationStep : public Step {
    PhaseCorrelationOutput output;
public:
    explicit PhaseCorrelationStep(const PhaseCorrelationInput& input = {});
    bool execute() override;
    const PhaseCorrelationOutput& getOutput() const { return output; }
};

} // namespace stitching::steps
