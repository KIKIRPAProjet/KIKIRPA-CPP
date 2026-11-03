// include/frontend/PipelineJson.h
#pragma once

#include "core/config/pipeline_config.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace FrontendNs {

    using Json = nlohmann::json;

    // Création dynamique du JSON d'un pipeline, à appeler depuis le frontend.
    // Les structures sont celles de Config.h (Option, PipelineOption, StepConfig, PipelineConfig) :
    //   Pipeline 1--* Step ; Step 1--* Option { type, key, value } ; Step 1--* PipelineOption (entrées) ;
    //   Step 1--1 Signature (sorties : PipelineOption).
    // Cette partie ne fait que CRÉER le JSON : pas de lecture, pas de vérification de l'enchaînement des steps.

    // Crée un step à partir de son nom, ses options, ses entrées et ses sorties (signature).
    ConfigNs::StepConfig makeStep(std::string name,
                                  std::vector<ConfigNs::StepParameter> parameters = {},
                                  std::vector<ConfigNs::DataSlot> inputs = {},
                                  std::vector<ConfigNs::DataSlot> outputs = {});

    // Option dont la valeur est donnée sous forme de texte (champ de formulaire), convertie selon `type` :
    // "string", "int", "float" ou "bool". Lève std::invalid_argument si la valeur ne correspond pas au type
    // ou si le type est inconnu.
    ConfigNs::StepParameter parameterFromText(const std::string& valueType, const std::string& name, const std::string& text);

    // { "pipeline": { "steps": [ ... ] } }
    Json pipelineToJson(const ConfigNs::PipelineConfig& pipeline);

    // Même JSON sous forme de texte (indent < 0 : une seule ligne).
    std::string pipelineToString(const ConfigNs::PipelineConfig& pipeline, int indent = 2);

    // Écrit le JSON dans un fichier (crée les dossiers manquants). Lève std::runtime_error si l'écriture échoue.
    // À utiliser pour le script source du pipeline, pas pour un fichier d'état écrit par le backend.
    void writePipelineJson(const ConfigNs::PipelineConfig& pipeline, const std::filesystem::path& file, int indent = 2);

}
