#include <pipeline/validator.hpp>
#include <fstream>
#include <iostream>
#include <set>
// Comparer le rapport produit par la commande aux attentes du manifeste des scénarios.
int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("Expected manifest, file name, report and exit status");
        std::ifstream manifestFile(argv[1]), reportFile(argv[3]);
        pipeline::Json manifest, report; manifestFile >> manifest; reportFile >> report;
        const pipeline::Json* match = nullptr;
        for (const auto& item : manifest)
            if (item.at("file") == argv[2]) match = &item;
        if (!match) throw std::runtime_error("Scenario absent du manifeste");
        const auto& expected = *match;
        const auto status = expected.at("valid").get<bool>() ? "0" : "1";
        if (std::string(argv[4]) != status) throw std::runtime_error("Code de sortie inattendu");
        if (report.at("valid") != expected.at("valid")) throw std::runtime_error("Unexpected validity");
        for (const auto& field : {"errors", "warnings"}) {
            std::set<std::string> actual;
            for (const auto& item : report.at(field)) actual.insert(item.at("code").get<std::string>());
            const auto& codes = expected.at(std::string(field) == "errors" ? "errorCodes" : "warningCodes");
            std::set<std::string> wanted;
            for (const auto& code : codes) wanted.insert(code.get<std::string>());
            if (actual != wanted) throw std::runtime_error(std::string("Unexpected codes: ") + field + " " + report.dump());
        }
        if (expected.contains("executionOrder") && report.at("executionOrder") != expected.at("executionOrder"))
            throw std::runtime_error("Unexpected execution order");
        if (!expected.at("valid").get<bool>() && !report.at("executionOrder").is_null())
            throw std::runtime_error("Invalid pipeline exposes an order");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
