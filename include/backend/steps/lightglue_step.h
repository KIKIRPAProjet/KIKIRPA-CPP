// include/backend/steps/lightglue_step.h
#pragma once

#include "algorithms/stitching/FeatureMatcher.h"
#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace StepsNs {

    struct LightGlueInput {
        std::string modelPath;
        int minMatches = 4;
        float overlap = 0.2f;
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
            using MatcherFactory = std::function<std::unique_ptr<stitching::matching::FeatureMatcher>(const nlohmann::json&)>;

            // La fabrique optionnelle remplace seulement l'inférence dans les tests du step.
            explicit LightGlueStepClass(const LightGlueInput& input = {}, MatcherFactory factory = {});
            bool execute() override;
            const LightGlueOutput& getOutput() const { return output; }

        private:
            LightGlueInput readInput() const;
            void loadMatcher(const std::string& modelPath);

            MatcherFactory factory;
            std::unique_ptr<stitching::matching::FeatureMatcher> matcher;
            std::string loadedModel;
            LightGlueOutput output;
    };

}
