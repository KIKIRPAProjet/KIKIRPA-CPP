// include/backend/core/pipeline/pipeline_report.h
#pragma once

#include "core/step/data_store.h"
#include "core/step/step.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

namespace PipelineNs {

    // Construit et écrit l'archive JSON ainsi que le rapport HTML lisible.
    nlohmann::json createStepInputs(const StepNs::StepClass& step, const StepNs::DataStoreClass& dataStore);
    nlohmann::json createStepReport(size_t stepIndex, const StepNs::StepClass& step,
                                    const StepNs::DataStoreClass& dataStore, const nlohmann::json& inputs);
    std::string makeStepSummary(const nlohmann::json& stepReport);
    void writeReport(const std::filesystem::path& reportPath, const nlohmann::json& reportJson);
    void writeHtmlReport(const std::filesystem::path& reportPath, const nlohmann::json& reportJson);

}
