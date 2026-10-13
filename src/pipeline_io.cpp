#include <pipeline/pipeline_validator.hpp>
#include "diagnostic_utils.hpp"
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pipeline {
using detail::issue;
ValidationReport PipelineValidator::validateText(const std::string& text) const {
    try {
        // Refuser les clés d’objet répétées afin d’éviter une configuration ambiguë.
        std::vector<std::set<std::string>> keys;
        auto callback = [&](int, Json::parse_event_t event, Json& parsed) {
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            if (event == Json::parse_event_t::key && !keys.back().insert(parsed.get<std::string>()).second)
                throw std::invalid_argument("Duplicate JSON object key: " + parsed.get<std::string>());
            if (event == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        };
        return validate(Json::parse(text, callback));
    } catch (const Json::parse_error& e) {
        ValidationReport report; report.errors.push_back(issue("INVALID_JSON", e.what(), {}, {{"byte", e.byte}})); return report;
    } catch (const std::invalid_argument& e) {
        ValidationReport report; report.errors.push_back(issue("INVALID_JSON", e.what())); return report;
    } catch (const Json::out_of_range& e) {
        ValidationReport report; report.errors.push_back(issue("INVALID_JSON", e.what())); return report;
    }
}
ValidationReport PipelineValidator::validateFile(const std::string& path) const {
    std::ifstream file(path);
    if (!file) { ValidationReport report; report.errors.push_back(issue("FILE_READ_ERROR", "Cannot open pipeline file.", {}, {{"path", path}})); return report; }
    std::ostringstream text; text << file.rdbuf();
    if (file.bad()) { ValidationReport report; report.errors.push_back(issue("FILE_READ_ERROR", "Cannot read pipeline file.")); return report; }
    return validateText(text.str());
}
}
