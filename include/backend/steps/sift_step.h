// include/backend/steps/sift_step.h
#pragma once

#include "core/step/step.h"
#include "core/data/algorithm_types.h"
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

    struct SiftOutput {
        std::string key = "matches";
        std::vector<DataNs::PairMatches> value;
        std::string type = "PairMatches";
    };

    void to_json(nlohmann::json& json, const SiftOutput& output);

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
