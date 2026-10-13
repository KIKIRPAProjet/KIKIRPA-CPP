#pragma once
#include <pipeline/validation_report.hpp>

namespace pipeline {
/** Description statique d’une étape, sans instance ni traitement métier. */
struct Step {
    std::string id, type;
    std::vector<std::string> dependencies;
    Json config = Json::object();
};
}
