// src/Backend/core/pipeline/pipeline_builder.cpp
#include "core/pipeline/pipeline_builder.h"

#include "core/config/pipeline_config.h"
#include "steps/GraphCutStep.h"
#include "steps/ImportStep.h"
#include "steps/PhaseCorrelationStep.h"

#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace PipelineNs {

    namespace {

        using StepCreator = std::function<std::unique_ptr<StepNs::StepClass>()>;

        // Une ligne par step. Le nom doit être celui que le step écrit dans sa config (stepConfig.name).
        const std::map<std::string, StepCreator>& stepRegistry() {
            static const std::map<std::string, StepCreator> registry = {
                {"import", [] { return std::make_unique<stitching::steps::ImportStep>(); }},
                {"phase-correlation", [] { return std::make_unique<stitching::steps::PhaseCorrelationStep>(); }},
                {"graphcut", [] { return std::make_unique<stitching::steps::GraphCutStep>(); }},
            };
            return registry;
        }

        // Structure générale du script : un objet avec "pipeline.steps" qui est un tableau
        void validateScriptStructure(const Json& scriptJson) {
            if (!scriptJson.is_object()) throw std::invalid_argument("pipeline script must be a JSON object");
            if (!scriptJson.contains("pipeline") || !scriptJson.at("pipeline").is_object())
                throw std::invalid_argument("field 'pipeline' must be an object");
            if (!scriptJson.at("pipeline").contains("steps") || !scriptJson.at("pipeline").at("steps").is_array())
                throw std::invalid_argument("field 'pipeline.steps' must be an array");
        }

        // Enchaînement : chaque entrée doit avoir été produite plus tôt, avec le même type
        void checkStepInputs(const ConfigNs::StepConfig& stepConfig, const std::string& stepPath, const std::string& stepName,
                             const std::map<std::string, std::string>& producedDataTypeByKey) {
            for (const ConfigNs::DataSlot& inputSlot : stepConfig.inputs) {
                const auto producedEntry = producedDataTypeByKey.find(inputSlot.key);
                if (producedEntry == producedDataTypeByKey.end())
                    throw std::invalid_argument(stepPath + " ('" + stepName + "'): input '" + inputSlot.key +
                                                "' is not produced by a previous step");
                if (producedEntry->second != inputSlot.dataType)
                    throw std::invalid_argument(stepPath + " ('" + stepName + "'): input '" + inputSlot.key + "' expects type '" +
                                                inputSlot.dataType + "', got '" + producedEntry->second + "'");
            }
        }

    }

    std::vector<std::string> getRegisteredStepNames() {
        std::vector<std::string> stepNames;
        for (const auto& registryEntry : stepRegistry()) stepNames.push_back(registryEntry.first);
        return stepNames;
    }

    std::unique_ptr<StepNs::StepClass> createStep(const std::string& stepName) {
        const auto registryEntry = stepRegistry().find(stepName);
        if (registryEntry == stepRegistry().end()) throw std::invalid_argument("unknown step '" + stepName + "'");
        return registryEntry->second();
    }

    PipelineClass buildPipeline(const Json& scriptJson) {
        validateScriptStructure(scriptJson);

        PipelineClass pipeline;
        std::map<std::string, std::string> producedDataTypeByKey;   // clé -> type des données produites par les steps précédents
        const Json& stepsJson = scriptJson.at("pipeline").at("steps");
        for (size_t stepIndex = 0; stepIndex < stepsJson.size(); ++stepIndex) {
            const Json& stepJson = stepsJson.at(stepIndex);
            const std::string stepPath = "pipeline.steps[" + std::to_string(stepIndex) + "]";
            if (!stepJson.is_object() || !stepJson.contains("name") || !stepJson.at("name").is_string())
                throw std::invalid_argument(stepPath + ": a step must be an object with a string 'name'");
            const std::string stepName = stepJson.at("name").get<std::string>();

            std::unique_ptr<StepNs::StepClass> step;
            try {
                step = createStep(stepName);
                step->importConfig(stepJson);
            } catch (const std::exception& exception) {
                throw std::invalid_argument(stepPath + ": " + exception.what());
            }

            const ConfigNs::StepConfig stepConfig = step->getConfigJson().get<ConfigNs::StepConfig>();
            checkStepInputs(stepConfig, stepPath, stepName, producedDataTypeByKey);
            for (const ConfigNs::DataSlot& outputSlot : stepConfig.outputs) producedDataTypeByKey[outputSlot.key] = outputSlot.dataType;

            pipeline.addStep(std::move(step));
        }
        if (scriptJson.contains("run")) pipeline.setRunState(RunState::fromJson(scriptJson.at("run")));   // état d'un run précédent
        return pipeline;
    }

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
            throw std::invalid_argument(scriptFile.string() + ": " + exception.what());
        }
    }

}
