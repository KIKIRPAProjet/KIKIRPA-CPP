#include "algorithms/matching/LightGlueMatcher.h"
#include <onnxruntime_cxx_api.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace stitching::matching {

LightGlueMatcher::LightGlueMatcher(const Json& params) : params(params) {
    // Read the model path passed by the pipeline configuration.
    const auto configuredPath = this->params.value("model_path", std::string{});
    if (configuredPath.empty()) {
        throw std::invalid_argument("LightGlue model_path is missing");
    }

    // First try the path as provided by the caller.
    std::filesystem::path modelPath = configuredPath;
    if (!modelPath.is_absolute() &&
        !std::filesystem::is_regular_file(modelPath)) {
        // If launched from build/, also try the project root directory.
        const auto projectRelativePath =
            std::filesystem::current_path().parent_path() / modelPath;
        if (std::filesystem::is_regular_file(projectRelativePath)) {
            modelPath = projectRelativePath;
        }
    }

    // Fail early with a clear error when the model file cannot be found.
    if (!std::filesystem::is_regular_file(modelPath)) {
        throw std::invalid_argument(
            "LightGlue model not found: " + configuredPath +
            " (working directory: " +
            std::filesystem::current_path().string() + ")");
    }

    // Keep the resolved path for the future ONNX Runtime session.
    this->params["model_path"] = modelPath.string();
    std::cout << "LightGlue model: " << modelPath.string() << '\n';

    // --- ÉTAPE 2 : INITIALISATION DU MODÈLE ONNX ---
    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(1);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
    
    this->session = std::make_unique<Ort::Session>(this->env, modelPath.c_str(), session_options);
    // -----------------------------------------------
}

bool LightGlueMatcher::match(const cv::Mat&, const cv::Mat&, const PairContext&, Matches&) {
    // --- ÉTAPE 3 : TEST DE LA SESSION LORS DE L'EXÉCUTION ---
    if (!this->session) {
        throw std::runtime_error("Erreur : La session ONNX n'est pas initialisée.");
    }

    size_t num_input_nodes = this->session->GetInputCount();
    std::cout << "[Test ONNX] Modèle actif. Nombre de tenseurs d'entrée attendus : " << num_input_nodes << "\n";
    // --------------------------------------------------------

    return true;
}

} // namespace stitching::matching
