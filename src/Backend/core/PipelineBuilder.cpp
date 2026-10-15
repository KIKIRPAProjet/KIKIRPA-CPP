#include "Backend/core/PipelineBuilder.h"
#include "core/Config.h"
#include "steps/AbsolutePositioningStep.h"
#include "steps/ExportStep.h"
#include "steps/FusionStep.h"
#include "steps/ImportStep.h"
#include "steps/IntensityCorrectionStep.h"
#include "steps/SeamlineStep.h"
#include "steps/StitchingStep.h"
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>

namespace stitching {

namespace {

using StepCreator = std::function<std::unique_ptr<Step>()>;

// Nom du step ("name" dans le JSON) → création du step avec sa config par défaut.
// Une ligne par step.
// Registre interne qui lie les noms JSON aux classes C++ correspondantes. Chaque step est créé avec sa config par défaut (ex. "import" → ImportStep avec ImportInput par défaut).
const std::map<std::string, StepCreator>& registry() {
    static const std::map<std::string, StepCreator> r = {
        {"import", [] { return std::make_unique<steps::ImportStep>(); }},
        {"stitching", [] { return std::make_unique<steps::StitchingStep>(); }},
        {"absolutePositioning", [] { return std::make_unique<steps::AbsolutePositioningStep>(); }},
        {"intensityCorrection", [] { return std::make_unique<steps::IntensityCorrectionStep>(); }},
        {"seamline", [] { return std::make_unique<steps::SeamlineStep>(); }},
        {"fusion", [] { return std::make_unique<steps::FusionStep>(); }},
        {"export", [] { return std::make_unique<steps::ExportStep>(); }},
    };
    return r;
}

} // namespace

Pipeline buildPipeline(const Json& script) {
    Pipeline p;
    std::map<std::string, std::string> produced;   // clé → type des données déjà produites

    for (const Json& stepJson : script.at("pipeline").at("steps")) { // On récupère la liste des steps du fichier "pipeline.json" (dans le champ "steps") et on parcourt cette liste

        // Pour chaque élément step, on lit le champ name, on cherche le step dans le registre interne (registry) et on associe les noms JSON aux classes C++. Ce registre est juste au dessus.

        const std::string name = stepJson.at("name").get<std::string>();
        const auto it = registry().find(name); // On cherche le nom du step (ex. "import", "stitching", etc.)
        if (it == registry().end()) throw std::invalid_argument("unknown step '" + name + "'"); // Si on ne trouve pas le step dans le registre, on lève une exception

        // Chaque step possède une config. par défaut dans son constructeur. importConfig() permet donc de mettre à jour cette config. par défaut avec ce qu'on a dans le JSON. La logique est :
        //  - le constructeur crée la configuration par défaut ;
        //  - le JSON fournit les valeurs personnalisées ;
        //  - seules les options connues sont acceptées ;
        //  - une option inconnue provoque une erreur.
        // Toutes les options qui ne sont pas dans le JSON gardent leur valeur par défaut.

        auto step = it->second();
        step->importConfig(stepJson);

        // Enchaînement : chaque entrée doit être la sortie d'un step précédent
        // On vérifie donc qu'une étape n'est pas manquante ou qu'une a été déplacée avant une autre (on ne peut pas faire stitching avant import)
        const StepConfig cfg = step->getConfig().get<StepConfig>();
        for (const PipelineOption& in : cfg.pipelineOptions) {
            const auto src = produced.find(in.key);
            if (src == produced.end())
                throw std::invalid_argument("step '" + name + "': input '" + in.key +
                                            "' is not produced by a previous step");
            if (src->second != in.type)
                throw std::invalid_argument("step '" + name + "': input '" + in.key + "' expects type '" +
                                            in.type + "', got '" + src->second + "'");
        }
        for (const PipelineOption& out : cfg.signature.pipelineOptions) produced[out.key] = out.type;

        p.add(std::move(step));
    }
    return p;
}

} // namespace stitching
