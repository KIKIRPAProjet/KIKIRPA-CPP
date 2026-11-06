// include/backend/steps/sift_step.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/step/step.h"


#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace StepsNs {

    struct SiftInput {
        static constexpr float DEFAULT_OVERLAP = 0.2f;
        static constexpr int DEFAULT_MAX_FEATURES = 500;
        static constexpr float DEFAULT_LOWE_RATIO = 0.75f;
        static constexpr double DEFAULT_CONTRAST_THRESHOLD = 0.01;
        static constexpr double DEFAULT_CLAHE_CLIP_LIMIT = 2.0;
        float overlap = DEFAULT_OVERLAP;
        int maxFeatures = DEFAULT_MAX_FEATURES;
        float loweRatio = DEFAULT_LOWE_RATIO;
        double contrastThreshold = DEFAULT_CONTRAST_THRESHOLD;
        double claheClipLimit = DEFAULT_CLAHE_CLIP_LIMIT;
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
