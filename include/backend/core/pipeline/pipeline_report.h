// include/backend/core/pipeline/pipeline_report.h
#pragma once

#include "core/step/data_store.h"
#include "core/step/step.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

namespace PipelineNs {

    // Sérialise les entrées configurées d'un step à partir des données disponibles.
    nlohmann::json createStepInputs(const StepNs::StepClass& step, const StepNs::DataStoreClass& dataStore);

    // Sérialise les seuls paramètres de configuration d'un step.
    nlohmann::json createReportConfiguration(const StepNs::StepClass& step);

    // Sérialise les paramètres, entrées, sorties et résultat d'un step.
    nlohmann::json createStepReport(size_t stepIndex, const StepNs::StepClass& step,
                                    const StepNs::DataStoreClass& dataStore, const nlohmann::json& inputs);

    // Produit un résumé lisible à partir d'une entrée de rapport.
    std::string makeStepSummary(const nlohmann::json& stepReport);

    // Écrit l'archive JSON ; lève une exception si le fichier ne peut pas être écrit.
    void writeReport(const std::filesystem::path& reportPath, const nlohmann::json& reportJson);

    // Écrit un fichier léger contenant les métadonnées nécessaires à la liste des exécutions.
    void writeReportSummary(const std::filesystem::path& reportPath, const nlohmann::json& reportJson);

    // Écrit le rapport HTML à partir d'une archive JSON ; lève une exception en cas d'échec d'écriture.
    void writeHtmlReport(const std::filesystem::path& reportPath, const nlohmann::json& reportJson);

}
