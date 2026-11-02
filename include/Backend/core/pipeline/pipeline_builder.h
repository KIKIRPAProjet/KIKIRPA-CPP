// include/Backend/core/pipeline/pipeline_builder.h
#pragma once

#include "core/pipeline/pipeline.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace PipelineNs {

    // Registre des steps : nom JSON (StepConfig::name) -> classe C++. Pour ajouter un step, une ligne dans
    // stepRegistry() (pipeline_builder.cpp) ; le constructeur par défaut du step fixe ses paramètres, entrées et sorties.

    // Noms des steps enregistrés, par ordre alphabétique.
    std::vector<std::string> getRegisteredStepNames();

    // Crée un step avec sa configuration par défaut. Lève std::invalid_argument si le nom est inconnu.
    std::unique_ptr<StepNs::StepClass> createStep(const std::string& stepName);

    // Construit le pipeline décrit par { "pipeline": { "steps": [...] }, "run": {...} } ("run" est facultatif) :
    // un step par entrée, dans l'ordre, créé d'après son "name" puis configuré par StepClass::importConfig.
    // Lève std::invalid_argument si le JSON est mal formé, si un step est inconnu ou mal configuré, ou si une entrée
    // d'un step n'est produite par aucun step précédent avec le même type.
    PipelineClass buildPipeline(const Json& scriptJson);

    // Lit le fichier JSON puis appelle buildPipeline. Lève std::runtime_error si le fichier est illisible ou le JSON
    // invalide, std::invalid_argument si le pipeline est invalide.
    PipelineClass buildPipelineFromFile(const std::filesystem::path& scriptFile);

}
