#include <pipeline/validator.hpp>
#include <regex>
#include "ordered_json.hpp"
namespace pipeline {
namespace {
ConfigField stringField(bool required = true, std::vector<Json> allowed = {}) {
    return {ConfigField::Type::String, required, {}, {}, std::move(allowed)};
}
}
// Ces règles constituent un exemple métier configurable, indépendant du moteur générique.
StepRegistry stitchingRegistry() {
    StepRegistry registry;
    StepRules import;
    import.configuration.fields = {{"path", stringField()}, {"regex", stringField(false)}};
    import.outputs = {"images"};
    import.customValidator = [](const Step& step) {
        std::vector<Diagnostic> errors;
        for (const auto& field : {"path", "regex"}) {
            if (!step.config.contains(field) || !step.config[field].is_string()) continue;
            auto value = step.config[field].get<std::string>();
            if (value.empty()) errors.push_back({"INVALID_CONFIGURATION", "Field must not be empty.", step.id, {{"field", field}}});
            if (std::string(field) == "regex") try { std::regex expression(value); }
            catch (const std::regex_error&) {
                errors.push_back({"INVALID_CONFIGURATION", "Invalid regular expression.", step.id, {{"field", field}}});
            }
        }
        return errors;
    };
    registry.registerType("ImportStep", import);
    StepRules stitching;
    stitching.dependencies = {{{"ImportStep"}, {}, false}};
    stitching.configuration.fields = {
        {"method", stringField(true, {"SIFT", "Harris-NCC", "LightGlue"})},
        {"overlap", {ConfigField::Type::Number, true, 0.0, 1.0, {}}},
        {"direction", stringField(true, {"LeftRight", "RightLeft", "TopBottom", "BottomTop"})}};
    stitching.outputs = {"relative_positions"}; stitching.acceptedInputs = {"images"};
    registry.registerType("StitchingStep", stitching);
    StepRules positioning;
    positioning.dependencies = {{{"StitchingStep"}, {}, false}};
    positioning.acceptedInputs = {"relative_positions"}; positioning.outputs = {"positioned_images"};
    registry.registerType("AbsolutePositioningStep", positioning);
    StepRules correction;
    correction.dependencies = {{{"AbsolutePositioningStep"}, {}, false}};
    correction.acceptedInputs = {"positioned_images"}; correction.outputs = {"corrected_images"};
    registry.registerType("IntensityCorrectionStep", correction);
    StepRules seamline;
    seamline.dependencies = {{{"AbsolutePositioningStep", "IntensityCorrectionStep"}, {}, false}};
    seamline.acceptedInputs = {"positioned_images", "corrected_images"}; seamline.outputs = {"seams"};
    registry.registerType("SeamlineStep", seamline);
    StepRules fusion;
    fusion.dependencies = {{{"AbsolutePositioningStep", "IntensityCorrectionStep"}, {}, false},
                           {{"SeamlineStep"}, {}, true}};
    fusion.acceptedInputs = {"positioned_images", "corrected_images", "seams"}; fusion.outputs = {"mosaic"};
    registry.registerType("FusionStep", fusion);
    StepRules exportRules;
    exportRules.dependencies = {{{"AbsolutePositioningStep", "IntensityCorrectionStep", "FusionStep"}, {}, false}};
    exportRules.configuration.fields = {{"output", stringField()}};
    exportRules.customValidator = [](const Step& step) {
        std::vector<Diagnostic> errors;
        if (step.config.contains("output") && step.config["output"].is_string() && step.config["output"].get<std::string>().empty())
            errors.push_back({"INVALID_CONFIGURATION", "Output path must not be empty.", step.id, {{"field", "output"}}});
        return errors;
    };
    exportRules.acceptedInputs = {"positioned_images", "corrected_images", "mosaic"};
    registry.registerType("ExportStep", exportRules);
    detail::registerOrderedTypes(registry);
    return registry;
}
}
