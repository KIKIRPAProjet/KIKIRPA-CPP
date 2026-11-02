#include "core/Step.h"
#include <stdexcept>

namespace stitching {

Json Step::option(const std::string& key) const {
    if (config.is_object() && config.contains("options") && config.at("options").is_array())
        for (const auto& opt : config.at("options"))
            if (opt.is_object() && opt.value("key", "") == key)
                return opt.at("value");
    const std::string name = config.is_object() && config.contains("name") && config.at("name").is_string()
                                 ? config.at("name").get<std::string>() : "?";
    throw std::out_of_range("Step '" + name + "': no option '" + key + "'");
}

} // namespace stitching
