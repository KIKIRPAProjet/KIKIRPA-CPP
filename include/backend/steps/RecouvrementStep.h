#pragma once

#include "algorithms/stitching/overlap_estimation/overlap_estimator.h"
#include "core/data/tile.h"
#include "core/step/step.h"

#include <opencv2/core.hpp>

#include <vector>

namespace StepsNs {

// Paramètres du step d'estimation du recouvrement.
struct RecouvrementInput {
    float overlap = 0.2f; // recouvrement nominal attendu entre deux voisines, fraction de la tuile
    int niveau = 0;        // niveau de pyramide analysé : 0 = pleine résolution
    PhaseCorrelationNs::Parameters parametres;
};

// Estimation d'une paire voisine. Les zones sont exprimées en pixels de pleine résolution.
struct RecouvrementPair {
    int tileIdA = 0;
    int tileIdB = 0;
    DataNs::Direction direction = DataNs::Direction::Right;
    DataNs::OverlapEstimate estimation;
    cv::Rect zoneA; // zone commune dans le repère de A
    cv::Rect zoneB; // zone commune dans le repère de B
};

struct RecouvrementOutput {
    std::vector<RecouvrementPair> pairs;
};

// Estime la translation et la zone commune pour les voisines droite et dessous de chaque tuile.
//
// Nom : "recouvrement". Entrée : "Tiles" ("tiles").
// Sortie publiée dans le DataStore : "OverlapEstimates" ("overlap-estimates").
//
// Convention : translationBInA est le coin haut-gauche de B dans le repère de A,
// en pixels de pleine résolution, x vers la droite et y vers le bas. zoneA et zoneB
// sont des cv::Rect de même taille dans les repères locaux respectifs des deux images.
// Une paire non estimable est listée dans getResult()["failures"]. Le step échoue si
// aucune paire n'est estimable, si les paramètres sont invalides ou si une image est illisible.
class RecouvrementStepClass : public StepNs::StepClass {
public:
    explicit RecouvrementStepClass(const RecouvrementInput& initialInput = {});
    bool execute() override;
    const RecouvrementOutput& getOutput() const { return output; }

    // Lit l'image source et la réduit jusqu'au niveau demandé.
    static cv::Mat readPixels(const DataNs::TileClass& tile, int level);

private:
    RecouvrementOutput output;
};

} // namespace StepsNs
