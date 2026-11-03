// src/backend/core/checkpoint/checkpoint.cpp
#include "core/checkpoint/checkpoint.h"

#include "core/checkpoint/checkpoint_types.h"
#include "core/config/pipeline_config.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace CheckpointNs {

    using Json = nlohmann::json;
    namespace fs = std::filesystem;

    namespace {

        // (Dé)sérialisation d'un type de donnée.
        struct Serializer {
            CheckpointClass::Saver saver;
            CheckpointClass::Loader loader;
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
        char prefix[32];
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

        bool complete = true;
        Json outputs = Json::array();   // sorties effectivement sauvegardées, listées dans le manifest
        for (const ConfigNs::DataSlot& outputSlot : stepConfig.outputs) {
            if (!dataStore.has(outputSlot.key)) continue;   // sortie non produite : rien à sauver
            const auto serializer = serializers().find(outputSlot.dataType);
            if (serializer == serializers().end()) {
                failureReason = "no serializer for type '" + outputSlot.dataType + "' (key '" + outputSlot.key + "')";
                complete = false;
                continue;
            }
            try {
                serializer->second.saver(dataStore, outputSlot.key, directory);
                outputs.push_back({{"key", outputSlot.key}, {"dataType", outputSlot.dataType}});
            } catch (const std::exception& exception) {
                failureReason = "cannot save '" + outputSlot.key + "': " + exception.what();
                complete = false;
            }
        }
        // Le manifest enregistre de quoi décider plus tard si le checkpoint est encore bon (voir valid) : nom, paramètres,
        // empreinte des entrées externes, sorties sauvegardées et indicateur de complétude.
        const Json manifest = {{"step", stepConfig.name}, {"parameters", step.getConfigJson().value("parameters", Json::array())},
                            {"inputs", step.externalInputsFingerprint()}, {"outputs", outputs}, {"complete", complete}};
        writeAtomic(directory / "manifest.json", manifest.dump(2));   // en dernier : sa présence marque le checkpoint complet
        return complete;
    }

    // Compare le manifest sauvegardé à l'état actuel du step : tout écart (paramètre modifié, fichier source changé)
    // rend le checkpoint périmé et le step sera refait.
    bool CheckpointClass::valid(size_t stepIndex, const StepNs::StepClass& step) const {
        if (!enabled()) return false;
        const std::string stepName = step.getConfigJson().value("name", "");
        const Json manifest = readManifest(stepDir(stepIndex, stepName));
        return manifest.is_object() && manifest.value("complete", false) && manifest.value("step", "") == stepName &&
            manifest.value("parameters", Json()) == step.getConfigJson().value("parameters", Json::array()) &&   // paramètres inchangés
            manifest.value("inputs", Json()) == step.externalInputsFingerprint();                               // sources externes inchangées
    }

    // Retrouve la sortie dans le manifest du step, puis délègue la lecture au chargeur de son type.
    void CheckpointClass::load(size_t stepIndex, const StepNs::StepClass& step, const std::string& key, const std::string& dataType,
                        StepNs::DataStoreClass& dataStore) const {
        const fs::path directory = stepDir(stepIndex, step.getConfigJson().value("name", ""));
        const Json manifest = readManifest(directory);
        if (!manifest.is_object() || !manifest.contains("outputs")) throw std::runtime_error("missing or corrupted " + (directory / "manifest.json").string());
        // Une clé absente du manifest n'a pas été produite par le step : rien à restaurer.
        for (const Json& outputEntry : manifest.at("outputs")) {
            if (outputEntry.value("key", "") != key) continue;
            const auto serializer = serializers().find(dataType);
            if (serializer == serializers().end()) throw std::runtime_error("no serializer for type '" + dataType + "'");
            serializer->second.loader(dataStore, key, directory);
            return;
        }
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
