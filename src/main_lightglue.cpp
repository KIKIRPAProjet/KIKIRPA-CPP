#include "core/PipelineBuilder.h"
#include "steps/LightGlueStep.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: stitching_lightglue <image-directory> <model.onnx> [filename-regex]\n";
        return 2;
    }
    try {
        stitching::PipelineSettings settings;
        settings.imageDir = argv[1];
        settings.modelPath = argv[2];
        if (argc == 4) settings.format = argv[3];
        auto pipeline = stitching::buildLightGluePipeline(settings);
        if (!pipeline.run([](const std::string& message) { std::cout << message << '\n'; })) return 1;
        const auto& pairs = pipeline.getData().get<stitching::steps::PairMatches>("matches");
        for (const auto& pair : pairs)
            std::cout << pair.a.path.filename().string() << " -> " << pair.b.path.filename().string()
                      << ": " << pair.matches.pointsA.size() << " matches, dx=" << pair.matches.dx
                      << ", dy=" << pair.matches.dy << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "LightGlue pipeline: " << e.what() << '\n';
        return 1;
    }
}
