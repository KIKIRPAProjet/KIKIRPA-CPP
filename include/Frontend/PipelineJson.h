#pragma once
#include "core/Config.h"
#include <filesystem>
#include <string>
#include <vector>

namespace stitching {

// Création dynamique du JSON d'un pipeline, à appeler depuis le frontend.
// Les structures sont celles de Config.h (Option, PipelineOption, StepConfig, PipelineConfig) :
//   Pipeline 1--* Step ; Step 1--* Option { type, key, value } ; Step 1--* PipelineOption (entrées) ;
//   Step 1--1 Signature (sorties : PipelineOption).
// Cette partie ne fait que CRÉER le JSON : pas de lecture, pas de vérification de l'enchaînement des steps.

// Crée un step à partir de son nom, ses options, ses entrées et ses sorties (signature).
StepConfig makeStep(std::string name,
                    std::vector<Option> options = {},
                    std::vector<PipelineOption> inputs = {},
                    std::vector<PipelineOption> outputs = {});

// Option dont la valeur est donnée sous forme de texte (champ de formulaire), convertie selon `type` :
// "string", "int", "float" ou "bool". Lève std::invalid_argument si la valeur ne correspond pas au type
// ou si le type est inconnu.
Option optionFromText(const std::string& type, const std::string& key, const std::string& text);

// { "pipeline": { "steps": [ ... ] } }
Json pipelineToJson(const PipelineConfig& pipeline);

// Même JSON sous forme de texte (indent < 0 : une seule ligne).
std::string pipelineToString(const PipelineConfig& pipeline, int indent = 2);

// Écrit le JSON dans un fichier (crée les dossiers manquants). Lève std::runtime_error si l'écriture échoue.
// À utiliser pour le script source du pipeline, pas pour un fichier d'état écrit par le backend.
void writePipelineJson(const PipelineConfig& pipeline, const std::filesystem::path& file, int indent = 2);

} // namespace stitching
