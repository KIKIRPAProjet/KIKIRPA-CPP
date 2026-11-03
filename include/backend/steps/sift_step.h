// include/backend/steps/sift_step.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace StepsNs {

    struct SiftInput {
        float overlap = 0.2f;
        int maxFeatures = 500;
        float loweRatio = 0.75f;
        double contrastThreshold = 0.01;
        double claheClipLimit = 2.0;
        int minMatches = 4;
    };

    // Résultats détaillés dans le repère pleine résolution de chaque tuile.
    struct SiftOutput {
        std::string key = "matches";
        std::vector<DataNs::PairMatches> value;
        std::string type = "PairMatches";
    };

    // Sérialisation explicite des résultats ; getResult() reste un résumé pour le journal.
    void to_json(nlohmann::json& json, const SiftOutput& output);

    // Mesure les voisines droite/bas sur leurs zones communes. Une paire en échec est ignorée ;
    // execute() renvoie false si aucune ne réussit ou si la configuration est invalide.
    class SiftStepClass : public StepNs::StepClass {

        public:
            explicit SiftStepClass(const SiftInput& input = {});
            bool execute() override;
            const SiftOutput& getOutput() const { return output; }

        private:
            SiftInput readInput() const;
            SiftOutput output;
    };

}
