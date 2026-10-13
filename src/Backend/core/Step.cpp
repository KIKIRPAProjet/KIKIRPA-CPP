#include "core/Step.h"
#include "core/Config.h"
#include <algorithm>
#include <stdexcept>

namespace stitching {

Json Step::option(const std::string& key) const {
    if (config.is_object() && config.contains("options"))
        for (const auto& o : config.at("options"))
            if (o.value("key", "") == key) return o.at("value");
    const std::string name = config.is_object() ? config.value("name", "?") : "?";
    throw std::out_of_range("step '" + name + "': missing option '" + key + "'");
}

void Step::importConfig(const Json& cfg) {
    if (!config.is_object()) {   // step sans config par défaut
        config = cfg;
        return;
    }

    StepConfig own = config.get<StepConfig>();
    const StepConfig in = cfg.get<StepConfig>();
    if (!in.name.empty() && in.name != own.name)
        throw std::invalid_argument("step '" + own.name + "': cannot import config of step '" + in.name + "'");

    for (const Option& o : in.options) {
        auto it = std::find_if(own.options.begin(), own.options.end(),
                               [&](const Option& d) { return d.key == o.key; });
        if (it == own.options.end())
            throw std::invalid_argument("step '" + own.name + "': unknown option '" + o.key + "'");
        it->value = o.value;
    }
    config = own;
}

} // namespace stitching
