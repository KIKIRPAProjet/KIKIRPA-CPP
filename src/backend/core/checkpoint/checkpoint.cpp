// src/backend/core/checkpoint/checkpoint.cpp
#include "core/checkpoint/checkpoint.h"

#include "core/checkpoint/checkpoint_types.h"
#include "core/config/pipeline_config.h"
#include "core/step/data_store.h"
#include "core/step/step.h"

// NOLINTBEGIN(misc-include-cleaner)
#include <nlohmann/json.hpp>
// NOLINTEND(misc-include-cleaner)

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace CheckpointNs {

    // NOLINTBEGIN(misc-include-cleaner)
    using Json = nlohmann::json;
    // NOLINTEND(misc-include-cleaner)
    namespace fs = std::filesystem; // NOLINT(readability-identifier-naming)

    namespace {

        constexpr std::size_t CHECKPOINT_DIRECTORY_PREFIX_LENGTH = 32;

        // (Dé)sérialisation d'un type de donnée.
        struct Serializer {
            CheckpointClass::Saver saver;
            CheckpointClass::Loader loader;
        };

        struct SaveContext {
            const StepNs::DataStoreClass& dataStore;
            const fs::path& directory;
            Json outputs = Json::array();
            std::string failureReason;
        };

        // Registre des (dé)sérialisations par type : d'abord celles du pipeline (pipelineTypes), puis celles ajoutées par registerType.
        std::map<std::string, Serializer>& serializers() {
            static std::map<std::string, Serializer> registry = [] {
                std::map<std::string, Serializer> initialSerializers;
                for (auto& [dataType, functions] : pipelineTypes()) initialSerializers[dataType] = {functions.first, functions.second};
                return initialSerializers;
            }();
            return registry;
        }

        // Écrit via un fichier temporaire puis renommage : un crash ne laisse jamais un fichier tronqué.
        void writeAtomic(const fs::path& filePath, const std::string& content) {
            const fs::path temporaryPath = filePath.string() + ".tmp";
            {
                std::ofstream output(temporaryPath, std::ios::binary);
                output << content;
                if (!output) throw std::runtime_error("cannot write " + temporaryPath.string());
            }
            fs::rename(temporaryPath, filePath);
        }

        // Manifest du dossier, ou JSON nul s'il est absent ou corrompu (sans exception).
        Json readManifest(const fs::path& directory) {
            std::ifstream input(directory / "manifest.json", std::ios::binary);
            if (!input) return Json();
            const Json manifest = Json::parse(input, nullptr, false);
            return manifest.is_discarded() ? Json() : manifest;
        }

        bool saveOutputSlot(const ConfigNs::DataSlot& outputSlot, SaveContext& context) {
            if (!context.dataStore.has(outputSlot.key))
                return true;
            const auto serializer = serializers().find(outputSlot.dataType);
            if (serializer == serializers().end()) {
                context.failureReason = "no serializer for type '" + outputSlot.dataType +
                                        "' (key '" + outputSlot.key + "')";
                return false;
            }
            try {
                serializer->second.saver(context.dataStore, outputSlot.key, context.directory);
                context.outputs.push_back(
                    {{"key", outputSlot.key}, {"dataType", outputSlot.dataType}});
                return true;
            } catch (const std::exception& exception) {
                context.failureReason = "cannot save '" + outputSlot.key + "': " +
                                        exception.what();
                return false;
            }
        }

        bool validOutputs(const Json& outputs, const ConfigNs::StepConfig& stepConfig,
                          const fs::path& directory) {
            if (!outputs.is_array())
                return false;
            for (const ConfigNs::DataSlot& output : stepConfig.outputs) {
                const auto savedOutput = std::find_if(
                    outputs.begin(), outputs.end(), [&output](const Json& entry) {
                        return entry.is_object() && entry.value("key", "") == output.key;
                    });
                if (savedOutput == outputs.end() ||
                    savedOutput->value("dataType", "") != output.dataType)
                    return false;
                const auto serializer = serializers().find(output.dataType);
                if (serializer == serializers().end())
                    return false;
                try {
                    StepNs::DataStoreClass probe;
                    serializer->second.loader(probe, output.key, directory);
                    if (!probe.has(output.key))
                        return false;
                } catch (const std::exception&) {
                    return false;
                }
            }
            return true;
        }

        const Json* findOutput(const Json& manifest, const std::string& key) {
            if (!manifest.is_object() || !manifest.contains("outputs") ||
                !manifest.at("outputs").is_array())
                return nullptr;
            for (const Json& output : manifest.at("outputs")) {
                if (output.is_object() && output.value("key", "") == key)
                    return &output;
            }
            return nullptr;
        }

    }

    // Ajoute (ou remplace) la paire sauvegarde/chargement d'un type dans le registre.
    void CheckpointClass::registerType(const std::string& dataType, Saver saver, Loader loader) {
        serializers()[dataType] = {std::move(saver), std::move(loader)};
    }

    bool CheckpointClass::hasType(const std::string& dataType) {
        return serializers().count(dataType) > 0;
    }

    // Ex. index 0 et nom "import" -> <racine>/00_import. Le préfixe numérique garde l'ordre des steps et permet à
    // removeFrom de retrouver l'index d'un dossier.
    fs::path CheckpointClass::stepDir(size_t stepIndex, const std::string& stepName) const {
        char prefix[CHECKPOINT_DIRECTORY_PREFIX_LENGTH];
        std::snprintf(prefix, sizeof prefix, "%02zu_", stepIndex);
        return rootDirectory / (prefix + stepName);
    }

    // 1. Repart d'un dossier vide (l'ancien checkpoint de ce step est supprimé) ; 2. sauvegarde chaque sortie déclarée
    // par le step ; 3. écrit le manifest. Une sortie impossible à sauver rend le checkpoint incomplet, sans interrompre les autres.
    bool CheckpointClass::save(size_t stepIndex, const StepNs::StepClass& step, const StepNs::DataStoreClass& dataStore, std::string& failureReason) const {
        if (!enabled()) return false;
        const ConfigNs::StepConfig stepConfig = step.getConfigJson().get<ConfigNs::StepConfig>();
        const fs::path directory = stepDir(stepIndex, stepConfig.name);
        fs::remove_all(directory);
        fs::create_directories(directory);

        SaveContext context{dataStore, directory};
        bool complete = true;
        for (const ConfigNs::DataSlot& outputSlot : stepConfig.outputs)
            if (!saveOutputSlot(outputSlot, context))
                complete = false;
        failureReason = context.failureReason;
        // Le manifest enregistre de quoi décider plus tard si le checkpoint est encore bon (voir valid) : nom, paramètres,
        // empreinte des entrées externes, sorties sauvegardées et indicateur de complétude.
        const Json manifest = {{"step", stepConfig.name}, {"parameters", step.getConfigJson().value("parameters", Json::array())},
                            {"inputs", step.externalInputsFingerprint()}, {"outputs", context.outputs},
                            {"complete", complete}};
        writeAtomic(directory / "manifest.json", manifest.dump(2));   // en dernier : sa présence marque le checkpoint complet
        return complete;
    }

    // Compare le manifest sauvegardé à l'état actuel du step : tout écart (paramètre modifié, fichier source changé)
    // rend le checkpoint périmé et le step sera refait.
    bool CheckpointClass::valid(size_t stepIndex, const StepNs::StepClass& step) const {
        if (!enabled()) return false;
        const std::string stepName = step.getConfigJson().value("name", "");
        const Json manifest = readManifest(stepDir(stepIndex, stepName));
        if (!manifest.is_object())
            return false;

        try {
            const ConfigNs::StepConfig stepConfig = step.getConfigJson().get<ConfigNs::StepConfig>();
            if (!manifest.value("complete", false) || manifest.value("step", "") != stepName ||
                manifest.value("parameters", Json()) != step.getConfigJson().value("parameters", Json::array()) ||
                manifest.value("inputs", Json()) != step.externalInputsFingerprint())
                return false;
            return validOutputs(manifest.value("outputs", Json()), stepConfig,
                                stepDir(stepIndex, stepName));
        } catch (const std::exception&) {
            return false;
        }
    }

    // Retrouve la sortie dans le manifest du step, puis délègue la lecture au chargeur de son type.
    void CheckpointClass::load(size_t stepIndex, const StepNs::StepClass& step,
                               const ConfigNs::DataSlot& output,
                               StepNs::DataStoreClass& dataStore) const {
        const fs::path directory = stepDir(stepIndex, step.getConfigJson().value("name", ""));
        const Json manifest = readManifest(directory);
        if (!manifest.is_object() || !manifest.contains("outputs"))
            throw std::runtime_error("missing or corrupted " +
                                     (directory / "manifest.json").string());
        const Json* savedOutput = findOutput(manifest, output.key);
        if (!savedOutput)
            return;
        if (savedOutput->value("dataType", "") != output.dataType)
            throw std::runtime_error("checkpoint data type does not match output '" +
                                     output.key + "'");
        const auto serializer = serializers().find(output.dataType);
        if (serializer == serializers().end())
            throw std::runtime_error("no serializer for type '" + output.dataType + "'");
        serializer->second.loader(dataStore, output.key, directory);
    }

    void CheckpointClass::load(size_t stepIndex, const StepNs::StepClass& step,
                               const std::string& key, const std::string& dataType,
                               StepNs::DataStoreClass& dataStore) const {
        load(stepIndex, step, ConfigNs::DataSlot{dataType, key}, dataStore);
    }

    void CheckpointClass::removeFrom(size_t firstStepIndex) const {
        if (!enabled() || !fs::is_directory(rootDirectory)) return;
        // Les dossiers de steps se nomment "NN_nom" : on supprime ceux dont NN >= firstStepIndex.
        for (const auto& entry : fs::directory_iterator(rootDirectory)) {
            const std::string entryName = entry.path().filename().string();
            if (entry.is_directory() && entryName.size() > 3 && entryName[2] == '_' && std::isdigit(static_cast<unsigned char>(entryName[0])) &&
                std::isdigit(static_cast<unsigned char>(entryName[1])) && static_cast<size_t>(std::stoi(entryName.substr(0, 2))) >= firstStepIndex)
                fs::remove_all(entry.path());
        }
    }

}
