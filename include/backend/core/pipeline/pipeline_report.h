// include/backend/core/pipeline/pipeline_report.h
#pragma once

#include "core/pipeline/pipeline.h"

#include <cstddef>
#include <filesystem>

namespace PipelineNs {

    // Construit et écrit l'archive JSON ainsi que le rapport HTML lisible.
    Json createStepInputs(const StepNs::StepClass& step, const StepNs::DataStoreClass& dataStore);
    Json createStepReport(size_t stepIndex, const StepNs::StepClass& step,
                          const StepNs::DataStoreClass& dataStore, const Json& inputs);
    std::string makeStepSummary(const std::string& stepName, const Json& outputs, const Json& result);
    void writeReport(const std::filesystem::path& reportPath, const Json& reportJson);
    void writeHtmlReport(const std::filesystem::path& reportPath, const Json& reportJson);

}
