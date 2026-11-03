#pragma once

#include "core/Step.h"
#include "core/Types.h"
#include "algorithms/stitching/recouvrement/EstimationRecouvrement.h"
#include <opencv2/core.hpp>
#include <vector>

namespace stitching::steps {

// Entrée du step (options de configuration)
struct RecouvrementInput {
    float overlap = 0.2f;   // recouvrement nominal (fraction de la tuile dans ]0, 1[)
    matching::correlation_phase::Parametres parametres;
};

// Sortie du step (signature)
struct RecouvrementOutput {
    std::vector<PairRecouvrement> pairs;
};

// Estime le recouvrement des tuiles voisines (droite et dessous) après l'import.
// Entrée DataStore : "tiles" (Tiles).
// Sortie DataStore : "recouvrements" (std::vector<PairRecouvrement> / Recouvrements).
//
// Convention de repère :
//   - Origine (0, 0) : coin supérieur gauche de chaque tuile.
//   - Axe X : horizontal vers la droite (colonnes [0, width - 1]).
//   - Axe Y : vertical vers le bas (lignes [0, height - 1]).
//   - Unités : pixels entiers (translation au pixel près, rectangles entiers cv::Rect).
//   - translation : coin haut-gauche de la tuile B dans le repère local de la tuile A.
//   - zoneA : bounding box de la zone commune dans le repère local de la tuile A.
//   - zoneB : bounding box de la zone commune dans le repère local de la tuile B.
//   - Propriété géométrique : zoneA.size() == zoneB.size(), zoneB = zoneA - round(translation).
//
// name : "recouvrement"
// options : overlap (float), lissage (float), seuilRapport (float), marge (float), fractionValideMin (float)
// pipelineOptions : { "type": "Tiles", "key": "tiles" }
// signature : { "type": "Recouvrements", "key": "recouvrements" }
class RecouvrementStep : public Step {
    RecouvrementOutput output;

public:
    explicit RecouvrementStep(const RecouvrementInput& input = {});
    // Surcharge de commodité (rétrocompatibilité avec signature d'origine)
    RecouvrementStep(float overlap,
                     const matching::correlation_phase::Parametres& parametres = {});

    bool execute() override;
    const RecouvrementOutput& getOutput() const { return output; }
};

} // namespace stitching::steps

