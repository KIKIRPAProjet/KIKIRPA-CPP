#pragma once
#include "core/DataStore.h"
#include "core/Step.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace stitching {

class Pipeline {
    std::vector<std::unique_ptr<Step>> steps;   // Pipeline --> Steps
    DataStore data;                             // sorties des steps déjà exécutés
public:
    using Logger = std::function<void(const std::string&)>;

    Pipeline() = default;
    // Lit { "pipeline": { "steps": [...] } } : steps[i] du JSON → i-ème step
    void configure(const Json& cfg);
    // Écrit le même format (script de reprise)
    Json exportConfig() const;
    void add(std::unique_ptr<Step> s) { steps.push_back(std::move(s)); }
    bool run(const Logger& log = {});
    const DataStore& getData() const { return data; }
};

} // namespace stitching
