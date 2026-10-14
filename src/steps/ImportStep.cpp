#include "steps/ImportStep.h"

namespace stitching::steps {

ImportStep::ImportStep(std::filesystem::path p, std::regex r)
    : path(std::move(p)), pattern(std::move(r)) {}

bool ImportStep::execute() { /* TODO : lire les images qui matchent pattern */ return true; }

} // namespace stitching::steps
