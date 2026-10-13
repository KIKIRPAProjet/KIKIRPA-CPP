#include <pipeline/validator.hpp>
#include <iostream>

// Dans le projet hôte, placer cette garde au début de Pipeline::run().
// Valider la même copie immuable que celle utilisée ensuite pour l’exécution.
int main() {
    pipeline::StepRegistry registry;
    pipeline::StepRules source;
    source.outputs = {"rows"};
    source.configuration.fields["table"] = {
        pipeline::ConfigField::Type::String, true, {}, {}, {}};
    registry.registerType("ReadTable", source);
    pipeline::StepRules transform;
    transform.dependencies = {{{"ReadTable"}, "source", false}};
    transform.acceptedInputs = {"rows"};
    transform.outputs = {"rows"};
    registry.registerType("Transform", transform);
    const pipeline::Json snapshot = {
        {"pipeline", {{"name", "ETL"}, {"version", "1.0"}, {"steps", {
            {{"id", "transform"}, {"type", "Transform"}, {"depends_on", {"source"}}, {"config", pipeline::Json::object()}},
            {{"id", "source"}, {"type", "ReadTable"}, {"depends_on", pipeline::Json::array()}, {"config", {{"table", "customers"}}}}
        }}}}
    };
    pipeline::PipelineValidator validator(std::move(registry));
    try {
        auto report = pipeline::requireValid(validator, snapshot);
        // Après la validation réussie, le pipeline hôte peut appeler ses exécuteurs.
        // Cet exemple affiche uniquement l’ordre, sans exécuter de traitement.
        for (const auto& id : *report.executionOrder) std::cout << id << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
