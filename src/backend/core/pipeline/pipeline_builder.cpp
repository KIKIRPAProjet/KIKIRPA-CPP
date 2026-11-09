// src/backend/core/pipeline/pipeline_builder.cpp
#include "core/pipeline/pipeline_builder.h"

#include "core/config/pipeline_config.h"
#include "core/step/step.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/run_state.h"
#include "steps/graph_cut_step.h"
#include "steps/import_step.h"
#include "steps/intensity_correction_step.h"
#include "steps/sift_step.h"
#include "steps/absolute_pos_step.h"
#include "steps/lightglue_step.h"
#include "steps/phase_correlation_step.h"
#include "steps/vote_2d_step.h"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace PipelineNs {

    namespace {

        using StepCreator = std::function<std::unique_ptr<StepNs::StepClass>()>;

        // Registre des steps : une ligne par step. Le nom doit être celui que le step écrit dans sa config (stepConfig.name).
        const std::map<std::string, StepCreator>& stepRegistry() {
            static const std::map<std::string, StepCreator> registry = {
                {"sift", [] { return std::make_unique<StepsNs::SiftStepClass>(); }},
                {"absolute-pos", [] { return std::make_unique<StepsNs::AbsolutePosStepClass>(); }},
                {"lightglue", [] { return std::make_unique<StepsNs::LightGlueStepClass>(); }},
                {"import", [] { return std::make_unique<StepsNs::ImportStepClass>(); }},
                {"phase-correlation", [] { return std::make_unique<StepsNs::PhaseCorrelationStepClass>(); }},
                {"graphcut", [] { return std::make_unique<StepsNs::GraphCutStepClass>(); }},
                {"intensity-correction", [] { return std::make_unique<StepsNs::IntensityCorrectionStepClass>(); }},
                {"vote-2d", [] { return std::make_unique<StepsNs::Vote2DStepClass>(); }},
            };
            return registry;
        }

        // Structure générale du script : un objet dont "pipeline.steps" est un tableau.
        void validateScriptStructure(const Json& scriptJson) {
            if (!scriptJson.is_object()) throw std::invalid_argument("pipeline script must be a JSON object");
            if (!scriptJson.contains("pipeline") || !scriptJson.at("pipeline").is_object())
                throw std::invalid_argument("field 'pipeline' must be an object");
            if (!scriptJson.at("pipeline").contains("steps") || !scriptJson.at("pipeline").at("steps").is_array())
                throw std::invalid_argument("field 'pipeline.steps' must be an array");
        }

        // Enchaînement des steps : chaque entrée doit avoir été produite par un step précédent, avec le même type.
        void checkStepInputs(const ConfigNs::StepConfig& stepConfig, const std::string& stepPath, const std::string& stepName,
                             const std::map<std::string, std::string>& producedDataTypeByKey) {
            for (const ConfigNs::DataSlot& inputSlot : stepConfig.inputs) {
                const auto producedEntry = producedDataTypeByKey.find(inputSlot.key);
                if (producedEntry == producedDataTypeByKey.end())
                    throw std::invalid_argument(std::string(stepPath).append(" ('").append(stepName).append("'): input '")
                                                .append(inputSlot.key).append("' is not produced by a previous step"));
                if (producedEntry->second != inputSlot.dataType)
                    throw std::invalid_argument(std::string(stepPath).append(" ('").append(stepName).append("'): input '")
                                                .append(inputSlot.key).append("' expects type '").append(inputSlot.dataType)
                                                .append("', got '").append(producedEntry->second).append("'"));
            }
        }

        std::unique_ptr<StepNs::StepClass> createConfiguredStep(const Json& stepJson, const std::string& stepPath) {
            if (!stepJson.is_object() || !stepJson.contains("name") || !stepJson.at("name").is_string())
                throw std::invalid_argument(stepPath + ": a step must be an object with a string 'name'");
            try {
                auto step = createStep(stepJson.at("name").get<std::string>());
                step->importConfig(stepJson);
                return step;
            } catch (const std::exception& exception) {
                throw std::invalid_argument(stepPath + ": " + exception.what());
            }
        }

    }

    // La std::map étant triée par clé, les noms sortent par ordre alphabétique.
    std::vector<std::string> getRegisteredStepNames() {
        std::vector<std::string> stepNames;
        for (const auto& registryEntry : stepRegistry()) stepNames.push_back(registryEntry.first);
        return stepNames;
    }

    // Appelle la fonction de création enregistrée pour ce nom ; le step est créé avec sa configuration par défaut.
    std::unique_ptr<StepNs::StepClass> createStep(const std::string& stepName) {
        const auto registryEntry = stepRegistry().find(stepName);
        if (registryEntry == stepRegistry().end()) throw std::invalid_argument("unknown step '" + stepName + "'");
        return registryEntry->second();
    }

    // Lit les steps dans l'ordre. Pour chacun : lecture du nom, création, configuration, puis vérification que ses entrées
    // sont produites plus haut dans le script. L'ordre compte : une entrée ne peut venir que d'un step précédent.
    PipelineClass buildPipeline(const Json& scriptJson) {
        validateScriptStructure(scriptJson);

        PipelineClass pipeline;
        std::map<std::string, std::string> producedDataTypeByKey;   // clé -> type des données produites par les steps déjà lus
        const Json& stepsJson = scriptJson.at("pipeline").at("steps");
        for (size_t stepIndex = 0; stepIndex < stepsJson.size(); ++stepIndex) {
            const Json& stepJson = stepsJson.at(stepIndex);
            const std::string stepPath = "pipeline.steps[" + std::to_string(stepIndex) + "]";   // préfixe des messages d'erreur
            auto step = createConfiguredStep(stepJson, stepPath);
            // Contrôle des entrées de ce step, puis enregistrement de ses sorties pour les steps suivants.
            // Une même clé produite deux fois garde le type du dernier producteur.
            const ConfigNs::StepConfig stepConfig = step->getConfigJson().get<ConfigNs::StepConfig>();
            checkStepInputs(stepConfig, stepPath, stepConfig.name, producedDataTypeByKey);
            for (const ConfigNs::DataSlot& outputSlot : stepConfig.outputs) producedDataTypeByKey[outputSlot.key] = outputSlot.dataType;

            pipeline.addStep(std::move(step));
        }
        if (scriptJson.contains("run")) pipeline.setRunState(RunState::fromJson(scriptJson.at("run")));   // état d'un run précédent
        return pipeline;
    }

    // Trois étapes : ouvrir le fichier, l'analyser en JSON, construire le pipeline. Chaque erreur nomme le fichier.
    PipelineClass buildPipelineFromFile(const std::filesystem::path& scriptFile) {
        std::ifstream input(scriptFile, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open " + scriptFile.string());
        Json scriptJson;
        try {
            scriptJson = Json::parse(input);
        } catch (const Json::parse_error& exception) {
            throw std::runtime_error("invalid JSON in " + scriptFile.string() + ": " + exception.what());
        }
        try {
            return buildPipeline(scriptJson);
        } catch (const std::invalid_argument& exception) {
            throw std::invalid_argument(scriptFile.string() + ": " + exception.what());   // indique quel fichier est en cause
        }
    }

}
