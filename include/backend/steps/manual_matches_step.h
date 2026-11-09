// include/backend/steps/manual_matches_step.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace StepsNs {

    struct ManualMatchesInput {
        // Corrections saisies par l'utilisateur, une par paire :
        //   [ { "tileIdA": 0, "tileIdB": 1, "pointsInA": [[x, y], ...], "pointsInB": [[x, y], ...] }, ... ]
        // Tableau vide : matches est transmis sans changement.
        nlohmann::json corrections = nlohmann::json::array();
    };

    struct ManualMatchesOutput {
        std::string key = "matches";
        std::vector<DataNs::PairMatches> value;
        std::string type = "PairMatches";
    };

    // Remplace les points appariés des paires corrigées à la main, puis recalcule leur translation
    // (médiane de pointsInA - pointsInB), seule valeur lue par absolute-pos. Les autres paires ne changent pas.
    // Les corrections sont des paramètres du step : en modifier une invalide son checkpoint, donc ce step et
    // les suivants sont refaits à la reprise, sans refaire l'import ni l'appariement.
    // Échoue (false, raison dans getResult()["error"]) si une correction désigne une paire absente de matches,
    // corrige deux fois la même paire, ou a des listes de points vides, de tailles différentes ou mal formées.
    class ManualMatchesStepClass : public StepNs::StepClass {

        public:
            explicit ManualMatchesStepClass(const ManualMatchesInput& input = {});
            bool execute() override;
            const ManualMatchesOutput& getOutput() const { return output; }

        private:
            ManualMatchesOutput output;
    };

}
