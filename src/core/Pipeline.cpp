#include "core/Pipeline.h"
#include <iostream>

namespace stitching {

void Pipeline::configure(const Json& cfg) {
    for (auto& s : steps) s->importConfig(cfg);
}

bool Pipeline::run(const Logger& log) {
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& s = steps[i];
        const std::string prefix = "Step " + std::to_string(i + 1) + "/" + std::to_string(steps.size());
        if (!s->execute()) {
            const std::string msg = prefix + " failed";
            std::cerr << msg << '\n';
            if (log) log(msg);
            return false;
        }
        if (log) log(prefix + " OK");
    }
    return true;
}

} // namespace stitching
