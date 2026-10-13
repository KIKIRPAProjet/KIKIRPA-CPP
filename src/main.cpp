#include <pipeline/validator.hpp>
#include <iostream>
// La commande affiche le rapport et renvoie 0 si valide, 1 si invalide, 2 si mal appelée.
int main(int argc, char** argv) {
    if (argc != 3 || std::string(argv[1]) != "validate") {
        std::cerr << "Usage: pipeline-validator validate pipeline.json\n"; return 2;
    }
    pipeline::PipelineValidator validator(pipeline::stitchingRegistry());
    auto report = validator.validateFile(argv[2]);
    std::cout << report.toJson().dump(2) << '\n';
    return report.valid() ? 0 : 1;
}
