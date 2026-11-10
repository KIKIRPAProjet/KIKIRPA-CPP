// include/backend/steps/vote_2d_step.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <string>
#include <vector>

namespace StepsNs {

    // Paramètres de l'input step de rejector Vote 2D, avec leurs valeurs par défaut.
    struct Vote2DInput {
        static constexpr float DEFAULT_SEUIL = 1.5f;
        static constexpr float DEFAULT_CASE = 1.0f;
        static constexpr int DEFAULT_MIN_MATCHES = 3;
        static constexpr int DEFAULT_MIN_INLIERS = 3;
        float seuil = DEFAULT_SEUIL;            // seuil de tolérance de distance euclidienne (px) pour qualifier un point d'inlier
        float cellSize = DEFAULT_CASE;          // taille des cellules de la grille de vote (px), paramètre JSON "case" (discrétisation des valeurs de translation)
        int minMatches = DEFAULT_MIN_MATCHES;   // nombre minimal de correspondances fournis en amont pour tenter l'estimation
        int minInliers = DEFAULT_MIN_INLIERS;   // nombre minimal d'inliers requis pour valider une paire de tuile
    };

    // Sortie du step : la clé, les PairMatches filtrés et leur types.
    struct Vote2DOutput {
        std::string key = "matches";
        std::vector<DataNs::PairMatches> value;
        std::string type = "PairMatches";
    };

    // Filtre les correspondances aberrantes de chaque paire de `matches` par vote de Hough 2D et médiane itérative
    // (RejectionNs::VoteRejectorClass), met à jour (translationX, translationY) et retire les paires invalides.
    class Vote2DStepClass : public StepNs::StepClass {

        public:
            explicit Vote2DStepClass(const Vote2DInput& input = {});
            bool execute() override;
            const Vote2DOutput& getOutput() const { return output; }

        private:
            Vote2DInput readInput() const;
            Vote2DOutput output;

    };

}
