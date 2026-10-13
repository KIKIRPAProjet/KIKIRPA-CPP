#include "ordered_json.hpp"
#include <regex>

namespace pipeline::detail {
namespace {
ConfigField field(ConfigField::Type type, std::vector<Json> values = {},
                  std::optional<double> min = {}, std::optional<double> max = {}) {
    return {type, true, min, max, std::move(values)};
}
}
void registerOrderedTypes(StepRegistry& registry) {
    using Type = ConfigField::Type;
    StepRules import;
    import.configuration.fields = {{"path", field(Type::String)}, {"format", field(Type::String)}};
    import.customValidator = [](const Step& step) {
        std::vector<Diagnostic> errors;
        if (step.config.contains("path") && step.config["path"].is_string() && step.config["path"].get<std::string>().empty())
            errors.push_back({"INVALID_CONFIGURATION", "Chemin vide.", step.id, {{"field", "path"}}});
        if (step.config.contains("format") && step.config["format"].is_string())
            try { std::regex pattern(step.config["format"].get<std::string>()); }
            catch (const std::regex_error&) { errors.push_back({"INVALID_CONFIGURATION", "Expression régulière invalide.", step.id, {{"field", "format"}}}); }
        return errors;
    };
    registry.registerType("import", import);
    StepRules stitching;
    stitching.dependencies = {{{"import"}, {}, false}};
    stitching.configuration.fields = {{"matcher", field(Type::String, {"sift", "harris-ncc", "lightglue"})},
        {"rejector", field(Type::String, {"ransac"})}, {"level", field(Type::Integer, {}, 0)},
        {"ratio", field(Type::Number, {}, 0, 1)}, {"threshold", field(Type::Number, {}, 0)}};
    registry.registerType("stitching", stitching);
    StepRules positioning;
    positioning.dependencies = {{{"stitching"}, {}, false}};
    positioning.configuration.fields = {{"referenceTile", field(Type::Integer, {}, 0)}};
    registry.registerType("absolutePositioning", positioning);
    StepRules correction;
    correction.dependencies = {{{"absolutePositioning"}, {}, false}};
    correction.configuration.fields = {{"enabled", field(Type::Boolean)}};
    registry.registerType("intensityCorrection", correction);
    StepRules seam;
    seam.dependencies = {{{"absolutePositioning", "intensityCorrection"}, {}, false}};
    seam.configuration.fields = {{"seamFinder", field(Type::String, {"graphcut"})}, {"level", field(Type::Integer, {}, 0)}};
    registry.registerType("seamline", seam);
    StepRules fusion;
    fusion.dependencies = {{{"absolutePositioning", "intensityCorrection"}, {}, false}, {{"seamline"}, {}, true}};
    fusion.configuration.fields = {{"blending", field(Type::String, {"multiband"})}, {"bands", field(Type::Integer, {}, 1)}};
    registry.registerType("fusion", fusion);
    StepRules exportRules;
    exportRules.dependencies = {{{"absolutePositioning", "intensityCorrection", "fusion"}, {}, false}};
    exportRules.configuration.fields = {{"path", field(Type::String)}, {"format", field(Type::String, {"bigtiff", "tiff"})},
        {"bits", field(Type::Integer, {8, 16, 32})}, {"blockSize", field(Type::Integer, {}, 1)},
        {"keepIcc", field(Type::Boolean)}, {"pyramid", field(Type::Boolean)}};
    registry.registerType("export", exportRules);
}
}
