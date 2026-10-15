#include "core/Pipeline.h"
#include "core/Config.h"
#include <iostream>
#include <stdexcept>

namespace stitching {

void Pipeline::configure(const Json& cfg) {
    const auto config = cfg.at("pipeline").get<PipelineConfig>();
    if (config.steps.size() != steps.size())
        throw std::invalid_argument("pipeline JSON has " + std::to_string(config.steps.size()) +
                                    " steps, expected " + std::to_string(steps.size()));
    for (size_t i = 0; i < steps.size(); ++i) steps[i]->importConfig(config.steps[i]);
}

Json Pipeline::exportConfig() const {
    PipelineConfig config;
    for (const auto& s : steps)
        config.steps.push_back(s->getConfig().is_object() ? s->getConfig().get<StepConfig>() : StepConfig{});
    return {{"pipeline", config}};
}

// L'exécution séquentielle
bool Pipeline::run(const Logger& log) {
    for (size_t i = 0; i < steps.size(); ++i) { // On boucle sur tous les steps
        const auto& s = steps[i]; // Le step courant
        const std::string prefix = "Step " + std::to_string(i + 1) + "/" + std::to_string(steps.size()); 
        s->setDataStore(data); // Association du DataStore au step courant
        if (!s->execute()) { // Exécution du step courant, si échec on sort de la boucle et on retourne false -> on arrête la pipeline
            const std::string msg = prefix + " failed: " + s->exportData().dump();
            std::cerr << msg << '\n';
            if (log) log(msg);
            return false;   // on peut reprendre plus tard grâce au JSON
        }
        if (log) log(prefix + " OK " + s->exportData().dump()); // Sinon, on passe au step suivant et on logge
    }
    return true;
}

} // namespace stitching
