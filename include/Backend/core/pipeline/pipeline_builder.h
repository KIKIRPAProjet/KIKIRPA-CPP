// include/Backend/core/pipeline/pipeline_builder.h
#pragma once

#include "core/pipeline/pipeline.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace PipelineNs {

    // Registre des steps : "name" du JSON → classe C++. Pour ajouter un step, une ligne dans registry()
    // (PipelineBuilder.cpp) ; son constructeur par défaut fixe ses options, ses entrées et ses sorties.

    // Noms des steps connus, dans l'ordre alphabétique
    std::vector<std::string> getRegisteredStepNames();

    // Crée un step avec sa config par défaut. Lève std::invalid_argument si le nom est inconnu.
    std::unique_ptr<StepNs::StepClass> createStep(const std::string& stepName);

    // Construit le pipeline décrit par { "pipeline": { "steps": [...] }, "run": {...} } (voir core/Config.h et
    // core/RunState.h ; "run" est facultatif) : un step par entrée, dans l'ordre, créé d'après son "name", puis
    // configuré par StepClass::importConfig.
    // Lève std::invalid_argument si le JSON est mal formé, si un step est inconnu ou mal configuré, ou si une entrée
    // d'un step (pipelineOptions) n'est produite par aucun step précédent avec le même type (signature).
    PipelineClass buildPipeline(const Json& scriptJson);

    // Lit le fichier puis appelle buildPipeline. Lève std::runtime_error si le fichier est illisible ou si le JSON
    // est mal formé, std::invalid_argument si le pipeline est invalide.
    PipelineClass buildPipelineFromFile(const std::filesystem::path& scriptFile);

}
