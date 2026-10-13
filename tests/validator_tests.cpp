#include <pipeline/validator.hpp>
#include <iostream>
#include <stdexcept>
using namespace pipeline;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
Json step(std::string id, std::string type = "A", std::vector<std::string> deps = {}, Json config = Json::object()) {
    return {{"id", id}, {"type", type}, {"depends_on", deps}, {"config", config}};
}
Json document(Json steps) { return {{"pipeline", {{"name", "Test"}, {"version", "1.0"}, {"steps", steps}}}}; }
StepRegistry registry() {
    StepRegistry r; r.registerType("A"); r.registerType("B");
    StepRules required; required.dependencies = {{{"A"}, {}, false}};
    r.registerType("Required", required);
    StepRules optional; optional.dependencies = {{{"A"}, {}, true}};
    r.registerType("Optional", optional);
    StepRules alternate; alternate.dependencies = {{{"A", "B"}, {}, false}};
    r.registerType("Alternate", alternate);
    return r;
}
bool has(const ValidationReport& report, const std::string& code) {
    for (const auto& error : report.errors) if (error.code == code) return true;
    return false;
}
}
// Chaque scénario est isolé : un échec est signalé sans interrompre les autres tests.
int main() {
    int passed = 0, failed = 0;
    auto test = [&](const char* name, auto body) {
        try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    PipelineValidator validator(registry());
    auto validate = [&](Json steps) { return validator.validate(document(std::move(steps))); };
    test("independent", [&] { check(validate({step("a")}).valid(), "independent invalid"); });
    test("multiple dependencies", [&] { check(validate({step("a"), step("b", "B"), step("c", "A", {"a", "b"})}).valid(), "multiple invalid"); });
    test("required absent", [&] { check(has(validate({step("r", "Required")}), "MISSING_DEPENDENCY"), "missing code"); });
    test("required present but unlinked", [&] { check(has(validate({step("a"), step("r", "Required")}), "MISSING_DEPENDENCY"), "presence is not a link"); });
    test("unknown type", [&] { check(has(validate({step("a", "Unknown")}), "UNKNOWN_STEP_TYPE"), "missing code"); });
    test("duplicate ID", [&] { check(has(validate({step("a"), step("a")}), "DUPLICATE_STEP_ID"), "missing code"); });
    test("self dependency", [&] { check(has(validate({step("a", "A", {"a"})}), "SELF_DEPENDENCY"), "missing code"); });
    test("direct cycle", [&] { auto r = validate({step("a", "A", {"b"}), step("b", "B", {"a"})}); check(has(r, "CYCLIC_DEPENDENCY") && !r.executionOrder && r.validatedSteps.empty(), "cycle accepted"); });
    test("indirect cycle", [&] { check(has(validate({step("a", "A", {"b"}), step("b", "B", {"c"}), step("c", "A", {"a"})}), "CYCLIC_DEPENDENCY"), "cycle accepted"); });
    test("branches", [&] { auto r = validate({step("d", "A", {"b"}), step("b"), step("c", "A", {"a"}), step("a")}); check(r.valid() && *r.executionOrder == std::vector<std::string>({"a", "b", "c", "d"}), "bad deterministic order"); });
    test("unordered", [&] { auto r = validate({step("r", "Required", {"a"}), step("a")}); check(r.valid() && *r.executionOrder == std::vector<std::string>({"a", "r"}), "bad order"); });
    test("invalid configuration", [&] { StepRegistry r = registry(); StepRules rules; rules.configuration.fields["n"] = {ConfigField::Type::Number, true, 0.0, 1.0, {}}; r.registerType("Config", rules); PipelineValidator v(r); check(has(v.validate(document({step("c", "Config", {}, {{"n", true}})})), "INVALID_CONFIGURATION"), "bool accepted as number"); check(has(v.validate(document({step("c", "Config", {}, {{"n", 2}})})), "INVALID_CONFIGURATION"), "range ignored"); check(has(v.validate(document({step("c", "Config")})), "INVALID_CONFIGURATION"), "required ignored"); });
    test("optional absent", [&] { check(validate({step("o", "Optional")}).valid(), "optional required"); });
    test("alternatives", [&] { check(validate({step("b", "B"), step("r", "Alternate", {"b"})}).valid(), "alternative rejected"); check(has(validate({step("r", "Alternate")}), "MISSING_DEPENDENCY"), "alternative missing ignored"); });
    test("multiple errors", [&] { auto r = validate({step("a", "Unknown", {"absent"}), step("r", "Required")}); check(has(r, "UNKNOWN_STEP_TYPE") && has(r, "UNKNOWN_DEPENDENCY") && has(r, "MISSING_DEPENDENCY"), "errors not accumulated"); });
    test("malformed JSON", [&] { check(has(validator.validateText("{bad"), "INVALID_JSON"), "parse error escaped"); });
    test("empty", [&] { check(has(validate(Json::array()), "INVALID_SCHEMA"), "empty accepted"); PipelineValidator v(registry(), true); check(v.validate(document(Json::array())).valid(), "empty policy ignored"); });
    test("multiple instances explicit", [&] { auto r = validate({step("a1"), step("a2"), step("r", "Required", {"a2"})}); check(r.valid(), "explicit instance rejected"); check(has(validate({step("a1"), step("a2"), step("r", "Required")}), "MISSING_DEPENDENCY"), "arbitrary selection"); });
    test("exact instance rule", [&] { StepRegistry r = registry(); StepRules rules; rules.dependencies = {{{"A"}, "a2", false}}; r.registerType("Exact", rules); PipelineValidator v(r); check(has(v.validate(document({step("a1"), step("a2"), step("r", "Exact", {"a1"})})), "MISSING_DEPENDENCY"), "wrong instance accepted"); check(v.validate(document({step("a1"), step("a2"), step("r", "Exact", {"a2"})})).valid(), "exact instance rejected"); });
    test("custom type and callback", [&] { auto r = registry(); StepRules rules; rules.customValidator = [](const Step& s) { return std::vector<Diagnostic>{{"INVALID_CONFIGURATION", "custom rule", s.id, {}}}; }; r.registerType("Custom", rules); check(has(PipelineValidator(r).validate(document({step("c", "Custom")})), "INVALID_CONFIGURATION"), "callback ignored"); });
    test("callback exception", [&] { auto r = registry(); StepRules rules; rules.customValidator = [](const Step&) -> std::vector<Diagnostic> { throw std::runtime_error("broken"); }; r.registerType("Custom", rules); check(has(PipelineValidator(r).validate(document({step("c", "Custom")})), "INVALID_CONFIGURATION"), "callback exception escaped"); });
    test("unknown reference", [&] { check(has(validate({step("a", "A", {"missing"})}), "UNKNOWN_DEPENDENCY"), "missing ref ignored"); });
    test("duplicate edge warning", [&] { auto r = validate({step("a"), step("b", "B", {"a", "a"})}); check(r.valid() && r.warnings.size() == 1 && r.executionOrder->size() == 2, "duplicate edge miscount"); });
    test("optional dangling still invalid", [&] { check(has(validate({step("o", "Optional", {"missing"})}), "UNKNOWN_DEPENDENCY"), "dangling optional accepted"); });
    test("output contract", [&] { auto r = registry(); StepRules producer; producer.outputs = {"text"}; r.registerType("Producer", producer); StepRules consumer; consumer.acceptedInputs = {"images"}; r.registerType("Consumer", consumer); check(has(PipelineValidator(r).validate(document({step("p", "Producer"), step("c", "Consumer", {"p"})})), "INCOMPATIBLE_STEP_OUTPUT"), "output mismatch ignored"); });
    test("schema errors", [&] { check(has(validator.validate(Json::array()), "INVALID_SCHEMA"), "root array accepted"); auto d = document({step("a")}); d["pipeline"]["steps"][0]["config"] = nullptr; d["pipeline"]["steps"][0]["depends_on"] = {42}; check(validator.validate(d).errors.size() == 2, "schema accumulation"); });
    test("duplicate JSON keys", [&] { check(has(validator.validateText(R"({"pipeline":{},"pipeline":{}})"), "INVALID_JSON"), "duplicate object key accepted"); });
    test("unreadable file", [&] { check(has(validator.validateFile("missing-file-for-test.json"), "FILE_READ_ERROR"), "file error ignored"); });
    test("invalid upstream propagation", [&] { auto r = validate({step("a", "Unknown"), step("b", "B", {"a"}), step("independent")}); check(r.validatedSteps == std::vector<std::string>{"independent"}, "invalid descendant marked valid"); });
    test("guard", [&] { bool threw = false; try { requireValid(validator, document({step("a", "Unknown")})); } catch (const std::runtime_error&) { threw = true; } check(threw, "guard allowed execution"); check(requireValid(validator, document({step("a")})).valid(), "guard rejected valid"); });
    test("large iterative graph", [&] { Json array = Json::array(); for (int i = 0; i < 5000; ++i) array.push_back(step(std::to_string(i), "A", i ? std::vector<std::string>{std::to_string(i-1)} : std::vector<std::string>{})); auto r = validate(array); check(r.valid() && r.executionOrder->size() == 5000, "large graph failed"); });
    test("stitching domain", [&] { PipelineValidator v(stitchingRegistry()); auto d = document({step("export", "ExportStep", {"positioning"}, {{"output", "final.tif"}}), step("positioning", "AbsolutePositioningStep", {"stitching"}), step("stitching", "StitchingStep", {"import"}, {{"method", "Harris-NCC"}, {"overlap", 0.2}, {"direction", "LeftRight"}}), step("import", "ImportStep", {}, {{"path", "./dataset"}})}); auto r = v.validate(d); check(r.valid() && *r.executionOrder == std::vector<std::string>({"import", "stitching", "positioning", "export"}), "stitching example rejected"); d["pipeline"]["steps"][2]["config"]["method"] = "Unknown"; check(has(v.validate(d), "INVALID_CONFIGURATION"), "method accepted"); });
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
