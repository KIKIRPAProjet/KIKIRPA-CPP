#pragma once
#include "core/Step.h"
#include <filesystem>
#include <regex>

namespace stitching::steps {

class ImportStep : public Step {
    std::filesystem::path path;
    std::regex pattern;
public:
    ImportStep(std::filesystem::path p, std::regex r);
    bool execute() override;
};

} // namespace stitching::steps
