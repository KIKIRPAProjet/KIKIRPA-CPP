#include "ordered_json.hpp"
#include "diagnostic_utils.hpp"
#include <algorithm>
#include <set>

namespace pipeline::detail {
Json normalizeOrdered(const Json& document, const StepRegistry& registry, ValidationReport& report) {
    Json result = {{"pipeline", {{"name", "Pipeline"}, {"version", "1.0"}, {"steps", Json::array()}}}};
    const auto& input = document["pipeline"];
    for (const auto& field : {"name", "version"})
        if (input.contains(field)) result["pipeline"][field] = input[field];
    std::set<std::string> names;
    std::vector<std::pair<std::string, std::string>> previous;
    for (std::size_t index = 0; index < input["steps"].size(); ++index) {
        const auto& item = input["steps"][index];
        if (!item.is_object() || !item.contains("name") || !item["name"].is_string() ||
            item["name"].get<std::string>().empty() || !item.contains("options") ||
            item.contains("id") || item.contains("type") || item.contains("depends_on") || item.contains("config")) {
            report.errors.push_back(issue("INVALID_SCHEMA", "Étape attendue : name et options, sans mélange des formats.", {}, {{"index", index}}));
            continue;
        }
        const auto name = item["name"].get<std::string>();
        if (!names.insert(name).second) {
            report.errors.push_back(issue("DUPLICATE_STEP_ID", "Le nom d’étape doit être unique dans ce format.", name));
            continue;
        }
        Json dependencies = Json::array();
        // La liaison précédente conserve l’ordre séquentiel, même sans règle métier.
        if (!previous.empty()) dependencies.push_back(previous.back().first);
        if (const auto* rules = registry.find(name)) {
            for (const auto& rule : rules->dependencies) {
                for (const auto& [id, type] : previous) {
                    if ((!rule.id || id == *rule.id) &&
                        std::find(rule.types.begin(), rule.types.end(), type) != rule.types.end() &&
                        std::find(dependencies.begin(), dependencies.end(), Json(id)) == dependencies.end())
                        dependencies.push_back(id);
                }
            }
        }
        auto config = readOptions(item["options"], name, report);
        result["pipeline"]["steps"].push_back({{"id", name}, {"type", name},
            {"depends_on", dependencies}, {"config", config}});
        previous.emplace_back(name, name);
    }
    return result;
}
}
