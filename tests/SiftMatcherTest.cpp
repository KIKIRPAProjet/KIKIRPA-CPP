#include "algorithms/matching/SiftMatcher.h"
#include <opencv2/imgcodecs.hpp>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: sift_matcher_test <image_a> <image_b>\n";
        return 2;
    }

    const std::string imageAPath = argv[1];
    const std::string imageBPath = argv[2];
    const cv::Mat imageA = cv::imread(imageAPath, cv::IMREAD_COLOR);
    const cv::Mat imageB = cv::imread(imageBPath, cv::IMREAD_COLOR);
    if (imageA.empty() || imageB.empty()) {
        std::cerr << "Could not read both images. Check the files and paths:\n"
                  << "  image A: " << imageAPath << '\n'
                  << "  image B: " << imageBPath << '\n';
        return 1;
    }

    try {
        stitching::matching::SiftMatcher matcher;
        stitching::Matches matches;
        const stitching::PairContext context{stitching::Direction::LeftRight, 0.3f};
        const bool success = matcher.match(imageA, imageB, context, matches);

        std::cout << "Image A: " << imageA.cols << 'x' << imageA.rows << '\n'
                  << "Image B: " << imageB.cols << 'x' << imageB.rows << '\n'
                  << "Expected overlap: 30% (right edge of A, left edge of B)\n"
                  << "Control-point correspondences: A=" << matches.pointsA.size()
                  << ", B=" << matches.pointsB.size() << '\n'
                  << "Estimated translation (dx, dy): ("
                  << matches.dx << ", " << matches.dy << ")\n";

        if (!success) {
            std::cerr << "SIFT did not find enough reliable matches (at least 4 required).\n";
            return 1;
        }
        if (matches.pointsA.size() != matches.pointsB.size()
            || matches.pointsA.size() != matches.scores.size()) {
            std::cerr << "SIFT returned inconsistent match data.\n";
            return 1;
        }

        std::cout << "SIFT test passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SIFT test failed: " << error.what() << '\n';
        return 1;
    }
}
