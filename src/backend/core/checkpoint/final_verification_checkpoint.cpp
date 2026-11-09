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

        void saveReport(const StepNs::DataStoreClass& dataStore, const std::string& key, const std::filesystem::path& directory) {
            std::ofstream output(directory / (key + ".json"), std::ios::binary);
            output << dataStore.get<DataNs::VerificationReport>(key).details.dump(2);
            if (!output) throw std::runtime_error("cannot write verification report checkpoint");
        }

        void loadReport(StepNs::DataStoreClass& dataStore, const std::string& key, const std::filesystem::path& directory) {
            std::ifstream input(directory / (key + ".json"), std::ios::binary);
            if (!input) throw std::runtime_error("cannot read verification report checkpoint");
            dataStore.replace(key, DataNs::VerificationReport{nlohmann::json::parse(input)});
        }

    }

    void registerFinalVerificationCheckpoint() {
        if (!CheckpointClass::hasType("VerificationReport"))
            CheckpointClass::registerType("VerificationReport", saveReport, loadReport);
    }

}
