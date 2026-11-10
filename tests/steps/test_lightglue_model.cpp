// tests/steps/test_lightglue_model.cpp
#include "steps/lightglue_step.h"

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/data_store.h"

#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

    constexpr int TILE_WIDTH = 333;
    constexpr int TILE_HEIGHT = 321;
    constexpr int TILE_SHIFT = 128;
    constexpr int SHAPE_COUNT = 400;
    constexpr int RANDOM_SEED = 42;
    constexpr int BORDER_MARGIN = 5;
    constexpr int MAXIMUM_RADIUS = 7;
    constexpr int BACKGROUND = 30;
    constexpr int COLOR_MINIMUM = 50;
    constexpr int COLOR_LIMIT = 256;
    constexpr double DEPTH_SCALE = 257.0;
    constexpr float POSITION_TOLERANCE = 3.f;
    constexpr double MINIMUM_INLIER_RATIO = 0.8;

    class DirectoryClass {
        public:
            DirectoryClass() {
                const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                path = std::filesystem::temp_directory_path() / ("lightglue-model-" + std::to_string(stamp));
                if (!std::filesystem::create_directory(path)) throw std::runtime_error("cannot create fixtures");
            }
            ~DirectoryClass() { std::error_code error; std::filesystem::remove_all(path, error); }
            std::filesystem::path path;
    };

    cv::Mat createCanvas() {
        cv::Mat canvas(TILE_HEIGHT, TILE_WIDTH + TILE_SHIFT, CV_8UC3, cv::Scalar(BACKGROUND, BACKGROUND, BACKGROUND));
        cv::RNG random(RANDOM_SEED);
        for (int index = 0; index < SHAPE_COUNT; ++index) {
            const cv::Point point(random.uniform(BORDER_MARGIN, canvas.cols - BORDER_MARGIN),
                                  random.uniform(BORDER_MARGIN, canvas.rows - BORDER_MARGIN));
            const cv::Scalar color(random.uniform(COLOR_MINIMUM, COLOR_LIMIT), random.uniform(COLOR_MINIMUM, COLOR_LIMIT),
                                   random.uniform(COLOR_MINIMUM, COLOR_LIMIT));
            cv::circle(canvas, point, random.uniform(2, MAXIMUM_RADIUS), color, -1);
        }
        return canvas;
    }

    DataNs::TileList createTiles(const cv::Mat& canvas, int depth, const std::filesystem::path& directory) {
        cv::Mat source;
        canvas.convertTo(source, depth, depth == CV_16U ? DEPTH_SCALE : 1.0);
        const cv::Mat imageA = source(cv::Rect(0, 0, TILE_WIDTH, TILE_HEIGHT));
        const cv::Mat imageB = source(cv::Rect(TILE_SHIFT, 0, TILE_WIDTH, TILE_HEIGHT));
        const auto pathA = directory / "a.tif", pathB = directory / "b.tif";
        if (!cv::imwrite(pathA.string(), imageA) || !cv::imwrite(pathB.string(), imageB))
            throw std::runtime_error("cannot write fixtures");
        auto tileA = std::make_shared<DataNs::TileClass>(0, pathA, "", imageA.cols, imageA.rows, imageA.type());
        auto tileB = std::make_shared<DataNs::TileClass>(1, pathB, "", imageB.cols, imageB.rows, imageB.type());
        tileA->setNeighbours(nullptr, tileB.get(), nullptr, nullptr);
        return {tileA, tileB};
    }

    bool hasExpectedGeometry(const DataNs::Matches& matches) {
        if (matches.pointsInA.size() < 4) return false;
        std::size_t correct = 0;
        for (std::size_t index = 0; index < matches.pointsInA.size(); ++index) {
            const auto delta = matches.pointsInA[index] - matches.pointsInB[index];
            if (std::abs(delta.x - static_cast<float>(TILE_SHIFT)) <= POSITION_TOLERANCE &&
                std::abs(delta.y) <= POSITION_TOLERANCE) ++correct;
        }
        return std::abs(matches.translationX - static_cast<float>(TILE_SHIFT)) <= POSITION_TOLERANCE &&
               std::abs(matches.translationY) <= POSITION_TOLERANCE &&
               static_cast<double>(correct) / static_cast<double>(matches.pointsInA.size()) >= MINIMUM_INLIER_RATIO;
    }

    bool runModelTest(const std::string& modelPath) {
        const cv::Mat canvas = createCanvas();
        const DirectoryClass directory;
        StepNs::DataStoreClass data;
        StepsNs::LightGlueStepClass step({modelPath, 4, static_cast<float>(TILE_WIDTH - TILE_SHIFT) / TILE_WIDTH});
        step.setDataStore(data);
        for (int depth : {CV_8U, CV_16U}) {
            data.replace("tiles", createTiles(canvas, depth, directory.path));
            if (!step.execute() || !hasExpectedGeometry(step.getOutput().value.at(0).featureMatches)) {
                std::cerr << "Model geometry test failed: " << step.getResult().dump() << '\n';
                return false;
            }
        }
        return true;
    }

}

// NOLINTNEXTLINE(bugprone-exception-escape) : seul std::cerr, dans les catch, peut encore lever.
int main(int argumentCount, char** arguments) {
    if (argumentCount != 2) { std::cerr << "Usage: test_lightglue_model <model.onnx>\n"; return 2; }
    try {
        return runModelTest(arguments[1]) ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Unexpected model test failure\n";
        return 1;
    }
}
