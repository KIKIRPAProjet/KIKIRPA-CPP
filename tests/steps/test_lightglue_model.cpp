#include "steps/LightGlueStep.h"
#include "core/Tuile.h"
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <chrono>
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
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() / ("lightglue-model-" + std::to_string(stamp));
    if (!std::filesystem::create_directory(dir)) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{dir};
    stitching::DataStore data;
    stitching::steps::LightGlueStep step({argv[1]});
    step.setDataStore(data);
    for (int depth : {CV_8U, CV_16U}) {
        cv::Mat source;
        canvas.convertTo(source, depth, depth == CV_16U ? 257.0 : 1.0);
        const cv::Mat imageA = source(cv::Rect(0, 0, 333, 321)).clone();
        const cv::Mat imageB = source(cv::Rect(128, 0, 333, 321)).clone();
        const auto pathA = dir / "a.tif", pathB = dir / "b.tif";
        if (!cv::imwrite(pathA.string(), imageA) || !cv::imwrite(pathB.string(), imageB)) return 1;
        auto a = std::make_shared<stitching::Tuile>(0, pathA, "", imageA.cols, imageA.rows, imageA.type());
        auto b = std::make_shared<stitching::Tuile>(1, pathB, "", imageB.cols, imageB.rows, imageB.type());
        a->setVoisins(nullptr, b.get(), nullptr, nullptr);
        data.set("tiles", stitching::Tiles{a, b});
        if (!step.execute()) {
            std::cerr << step.exportData().dump(2) << '\n';
            return 1;
        }
        const auto& matches = step.getOutput().value.at(0).matches;
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
