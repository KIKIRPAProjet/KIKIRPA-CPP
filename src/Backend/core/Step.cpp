#include "core/Step.h"
#include <stdexcept>

namespace stitching {

Json Step::option(const std::string& key) const {
    if (config.contains("options") && config["options"].is_array()) {
        for (const auto& opt : config["options"]) {
            if (opt.contains("key") && opt["key"] == key) {
                if (opt.contains("value")) {
                    return opt["value"];
                }
            }
        }
    }
    // Cas de secours : si options est sous forme d'objet direct {"path": "..."}
    if (config.contains("options") && config["options"].is_object() && config["options"].contains(key)) {
        return config["options"][key];
    }
    throw std::out_of_range("Step::option: option non trouvee pour la cle '" + key + "'");
}

} // namespace stitching
