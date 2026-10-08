#pragma once
#include "core/Step.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace stitching {

class Pipeline {
    std::vector<std::unique_ptr<Step>> steps;   // Pipeline --> Steps
public:
    using Logger = std::function<void(const std::string&)>;

    Pipeline() = default;
    void configure(const Json& cfg);
    void add(std::unique_ptr<Step> s) { steps.push_back(std::move(s)); }
    bool run(const Logger& log = {});
};

} // namespace stitching
