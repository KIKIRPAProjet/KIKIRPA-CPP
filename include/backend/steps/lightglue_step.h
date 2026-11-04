// include/backend/steps/lightglue_step.h
#pragma once

#include "algorithms/stitching/feature_matcher.h"
#include "core/data/algorithm_types.h"
#include "core/step/step.h"


#include <functional>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace StepsNs {

    struct LightGlueInput {
        static constexpr float DEFAULT_OVERLAP = 0.2f;
        std::string modelPath;
        int minMatches = 4;
        float overlap = DEFAULT_OVERLAP;
    };

    // Coordonnées dans les repères pleine résolution de chaque tuile.
    struct LightGlueOutput {
        std::string key = "matches";
        std::vector<DataNs::PairMatches> value;
        std::string type = "PairMatches";
    };

    void to_json(nlohmann::json& json, const LightGlueOutput& output);

    // Apparie les zones communes des voisines droite/bas. Une paire en échec est ignorée ;
    // execute() renvoie false si aucune paire ne réussit ou si une erreur globale survient.
    class LightGlueStepClass : public StepNs::StepClass {

        public:
            using MatcherFactory = std::function<std::unique_ptr<StitchingNs::FeatureMatcherClass>(const nlohmann::json&)>;

            // La fabrique optionnelle remplace seulement l'inférence dans les tests du step.
            explicit LightGlueStepClass(const LightGlueInput& input = {}, MatcherFactory factory = {});
            bool execute() override;
            const LightGlueOutput& getOutput() const { return output; }

        private:
            LightGlueInput readInput() const;
            void loadMatcher(const std::string& modelPath);

            MatcherFactory factory;
            std::unique_ptr<StitchingNs::FeatureMatcherClass> matcher;
            std::string loadedModel;
            LightGlueOutput output;
    };

}
