#pragma once
#include "core/Step.h"
#include "core/Tuile.h"
#include <filesystem>
#include <regex>

namespace stitching::steps {

class ImportStep : public Step {
    std::filesystem::path path;
    std::regex pattern;
    Tuile& first;
    Tuile& second;
public:
    ImportStep(std::filesystem::path p, std::regex r, Tuile& a, Tuile& b);
    bool execute() override;
};

} // namespace stitching::steps
