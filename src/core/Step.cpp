#include "core/Step.h"
#include <stdexcept>

namespace stitching {

Json Step::option(const std::string& key) const {
    if (config.is_object() && config.contains("options"))
        for (const auto& o : config.at("options"))
            if (o.value("key", "") == key) return o.at("value");
    const std::string name = config.is_object() ? config.value("name", "?") : "?";
    throw std::out_of_range("step '" + name + "': missing option '" + key + "'");
}

} // namespace stitching
