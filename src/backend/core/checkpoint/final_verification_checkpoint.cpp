// src/backend/core/checkpoint/final_verification_checkpoint.cpp
#include "core/checkpoint/final_verification_checkpoint.h"

#include "core/checkpoint/checkpoint.h"
#include "core/data/verification_report.h"
#include "core/step/data_store.h"

#include <nlohmann/json_fwd.hpp>
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): parse nécessite le type JSON complet.

#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>

namespace CheckpointNs {

    namespace {

        // Écrit le JSON du rapport dans un fichier dédié au checkpoint.
        void saveReport(const StepNs::DataStoreClass& dataStore, const std::string& key, const std::filesystem::path& directory) {
            // Le rapport est déjà du JSON; le checkpoint conserve son contenu complet dans un fichier dédié.
            std::ofstream output(directory / (key + ".json"), std::ios::binary);
            output << dataStore.get<DataNs::VerificationReport>(key).details.dump(2);
            if (!output) throw std::runtime_error("cannot write verification report checkpoint");
        }

        // Relit le JSON et le republie sous le même type dans le DataStore restauré.
        void loadReport(StepNs::DataStoreClass& dataStore, const std::string& key, const std::filesystem::path& directory) {
            std::ifstream input(directory / (key + ".json"), std::ios::binary);
            if (!input) throw std::runtime_error("cannot read verification report checkpoint");
            // Reconstruire le même type que celui publié par le step pour qu'il reste lisible après reprise.
            dataStore.replace(key, DataNs::VerificationReport{nlohmann::json::parse(input)});
        }

    }

    // Rend disponibles les fonctions de sauvegarde et de restauration pour le type VerificationReport.
    void registerFinalVerificationCheckpoint() {
        if (!CheckpointClass::hasType("VerificationReport"))
            CheckpointClass::registerType("VerificationReport", saveReport, loadReport);
    }

}
