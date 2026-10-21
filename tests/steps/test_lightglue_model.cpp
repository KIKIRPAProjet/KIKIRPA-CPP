#include "steps/LightGlueStep.h"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <iostream>

// Vérification d'une translation connue avec un vrai modèle ONNX ; aucun fichier image écrit.
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: test_lightglue_model <model.onnx>\n";
        return 2;
    }
    // Dimensions non multiples de 32 pour vérifier aussi le retour aux coordonnées originales.
    cv::Mat canvas(321, 461, CV_8UC3, cv::Scalar(30, 30, 30));
    cv::RNG random(42);
    for (int i = 0; i < 400; ++i) {
        const cv::Point point(random.uniform(5, canvas.cols - 5), random.uniform(5, canvas.rows - 5));
        const cv::Scalar color(random.uniform(50, 256), random.uniform(50, 256), random.uniform(50, 256));
        cv::circle(canvas, point, random.uniform(2, 7), color, -1);
    }
    stitching::DataStore data;
    stitching::steps::LightGlueStep step({argv[1]});
    step.setDataStore(data);
    for (int depth : {CV_8U, CV_16U}) {
        cv::Mat source;
        canvas.convertTo(source, depth, depth == CV_16U ? 257.0 : 1.0);
        data.set("tiles", stitching::Tiles{
            {"synthetic-a", 0, 0, source(cv::Rect(0, 0, 333, 321)).clone()},
            {"synthetic-b", 1, 0, source(cv::Rect(128, 0, 333, 321)).clone()}
        });
        if (!step.execute()) {
            std::cerr << step.exportData().dump(2) << '\n';
            return 1;
        }
        const auto& matches = step.getOutput().at(0).matches;
        size_t correct = 0;
        for (size_t i = 0; i < matches.pointsA.size(); ++i) {
            const auto delta = matches.pointsA[i] - matches.pointsB[i];
            if (std::abs(delta.x - 128.f) <= 3.f && std::abs(delta.y) <= 3.f) ++correct;
        }
        if (std::abs(matches.dx - 128.f) > 3.f || std::abs(matches.dy) > 3.f ||
            correct < 4 || correct * 5 < matches.pointsA.size() * 4) {
            std::cerr << "Unexpected translation or fewer than 80% geometrically correct correspondences\n";
            return 1;
        }
        std::cout << "PASS real model (depth=" << depth << "): " << matches.pointsA.size() << " correspondences, dx="
                  << matches.dx << ", dy=" << matches.dy << '\n';
    }
    return 0;
}
