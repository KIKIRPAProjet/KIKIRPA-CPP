#include "LightGlueTestPipeline.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: stitching_lightglue <image-directory> <model.onnx> [filename-regex]\n";
        return 2;
    }
    try {
        stitching::steps::ImportInput input;
        input.path = argv[1];
        if (argc == 4) input.format = argv[3];
        const stitching::steps::LightGlueInput lightGlue{argv[2]};
        auto pipeline = stitching::tests::buildLightGlueTestPipeline(input, lightGlue);
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
