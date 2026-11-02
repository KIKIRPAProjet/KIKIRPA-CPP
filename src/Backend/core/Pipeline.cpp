#include "core/Pipeline.h"
#include "core/Config.h"
#include <iostream>
#include <stdexcept>

namespace stitching {

void Pipeline::configure(const Json& cfg) {
    if (!cfg.is_object() || !cfg.contains("pipeline") || !cfg.at("pipeline").is_object() ||
        !cfg.at("pipeline").contains("steps") || !cfg.at("pipeline").at("steps").is_array())
        throw std::invalid_argument("expected { \"pipeline\": { \"steps\": [...] } }");
    const Json& stepsJson = cfg.at("pipeline").at("steps");
    if (stepsJson.size() != steps.size())
        throw std::invalid_argument("pipeline JSON has " + std::to_string(stepsJson.size()) +
                                    " steps, expected " + std::to_string(steps.size()));
    for (size_t i = 0; i < steps.size(); ++i) {
        try {
            steps[i]->importConfig(stepsJson.at(i));
        } catch (const std::exception& e) {
            throw std::invalid_argument("pipeline.steps[" + std::to_string(i) + "]: " + e.what());
        }
    }
}

Json Pipeline::exportConfig() const {
    PipelineConfig config;
    for (const auto& s : steps)
        config.steps.push_back(s->getConfig().is_object() ? s->getConfig().get<StepConfig>() : StepConfig{});
    return {{"pipeline", config}};
}

bool Pipeline::run(const Logger& log) {
    data = DataStore();   // un run ne voit jamais les données d'un run précédent
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& s = steps[i];
        const std::string prefix = "Step " + std::to_string(i + 1) + "/" + std::to_string(steps.size());
        s->setDataStore(data);
        bool ok = false;
        std::string error;
        try {
            ok = s->execute();
        } catch (const std::exception& e) {   // une exception d'un step = un step en échec, pas un crash
            error = e.what();
        }
        if (!ok) {
            const std::string msg = prefix + " failed: " + (error.empty() ? s->exportData().dump() : error);
            if (log) log(msg);
            else std::cerr << msg << '\n';
            return false;   // on peut reprendre plus tard grâce au JSON
        }
        if (log) log(prefix + " OK " + s->exportData().dump());
    }
    return true;
}

} // namespace stitching
