#include "core/Step.h"
#include <stdexcept>

namespace stitching {

Json Step::option(const std::string& key) const {
    if (config.contains("options"))
        for (const auto& o : config.at("options"))
            if (o.value("key", "") == key) return o.at("value");
    throw std::out_of_range("Step '" + config.value("name", "") + "': no option '" + key + "'");
}

} // namespace stitching
