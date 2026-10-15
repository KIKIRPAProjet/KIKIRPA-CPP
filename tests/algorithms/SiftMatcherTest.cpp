#include "algorithms/matching/SiftMatcher.h"
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <opencv2/imgcodecs.hpp>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace {

struct TileImage {
    std::filesystem::path path;
    std::string tileId;
    int row;
    int column;
    cv::Mat image;
};

struct TileOffset {
    std::size_t tileA;
    std::size_t tileB;
    cv::Point2d delta;
};

struct PairResult {
    bool success;
    stitching::Matches matches;
};

bool isImageFile(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg"
        || extension == ".tif" || extension == ".tiff" || extension == ".bmp";
}

bool parseOverlap(const char* value, float& overlap) {
    try {
        std::size_t parsedCharacters = 0;
        overlap = std::stof(value, &parsedCharacters);
        return parsedCharacters == std::string(value).size()
            && overlap >= 0.f && overlap <= 1.f;
    } catch (const std::exception&) {
        return false;
    }
}

PairResult matchPair(const cv::Mat& imageA,
                     const cv::Mat& imageB,
                     stitching::Direction direction,
                     float overlap) {
    stitching::PairContext context{direction, overlap};
    PairResult result{};
    stitching::matching::SiftMatcher matcher;
    result.success = matcher.match(imageA, imageB, context, result.matches);
    return result;
}

bool estimateTilePositions(const std::vector<TileImage>& tiles,
                           const std::vector<TileOffset>& offsets,
                           std::vector<stitching::TilePositionData>& positions) {
    positions.clear();
    if (tiles.empty()) return false;

    std::vector<bool> connected(tiles.size(), false);
    connected[0] = true;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const TileOffset& offset : offsets) {
            if (connected[offset.tileA] && !connected[offset.tileB]) {
                connected[offset.tileB] = true;
                changed = true;
            } else if (connected[offset.tileB] && !connected[offset.tileA]) {
                connected[offset.tileA] = true;
                changed = true;
            }
        }
    }

    std::vector<int> variableIndex(tiles.size(), -1);
    int variableCount = 0;
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        if (connected[index] && index != 0) {
            variableIndex[index] = variableCount++;
        }
    }

    std::vector<const TileOffset*> usableOffsets;
    for (const TileOffset& offset : offsets) {
        if (connected[offset.tileA] && connected[offset.tileB]) {
            usableOffsets.push_back(&offset);
        }
    }

    std::vector<cv::Point2d> solved(tiles.size(), cv::Point2d(0.0, 0.0));
    if (variableCount > 0) {
        cv::Mat coefficients(
            static_cast<int>(usableOffsets.size()), variableCount, CV_64F, cv::Scalar(0));
        cv::Mat rightHandSidesX(
            static_cast<int>(usableOffsets.size()), 1, CV_64F);
        cv::Mat rightHandSidesY(
            static_cast<int>(usableOffsets.size()), 1, CV_64F);

        for (std::size_t row = 0; row < usableOffsets.size(); ++row) {
            const TileOffset& offset = *usableOffsets[row];
            if (offset.tileB != 0) {
                coefficients.at<double>(static_cast<int>(row), variableIndex[offset.tileB]) += 1.0;
            }
            if (offset.tileA != 0) {
                coefficients.at<double>(static_cast<int>(row), variableIndex[offset.tileA]) -= 1.0;
            }
            rightHandSidesX.at<double>(static_cast<int>(row), 0) = offset.delta.x;
            rightHandSidesY.at<double>(static_cast<int>(row), 0) = offset.delta.y;
        }

        cv::Mat solutionX;
        cv::Mat solutionY;
        if (!cv::solve(coefficients, rightHandSidesX, solutionX, cv::DECOMP_SVD)
            || !cv::solve(coefficients, rightHandSidesY, solutionY, cv::DECOMP_SVD)) {
            return false;
        }
        for (std::size_t index = 1; index < tiles.size(); ++index) {
            if (!connected[index]) continue;
            const int variable = variableIndex[index];
            solved[index] = cv::Point2d(
                solutionX.at<double>(variable, 0),
                solutionY.at<double>(variable, 0));
        }
    }

    for (std::size_t index = 0; index < tiles.size(); ++index) {
        if (connected[index]) {
            positions.push_back({tiles[index].tileId, solved[index]});
        }
    }
    return !positions.empty();
}

void printPositions(const std::vector<stitching::TilePositionData>& positions) {
    std::cout << std::setprecision(12)
              << "\nstd::vector<stitching::TilePositionData> tilePositions = {\n";
    for (const stitching::TilePositionData& tile : positions) {
        std::cout << "    {\"" << tile.tile_id << "\", {"
                  << tile.position.x << ", " << tile.position.y << "}},\n";
    }
    std::cout << "};\n";
}

int runDirectory(const std::filesystem::path& directory, float overlap) {
    if (!std::filesystem::is_directory(directory)) {
        std::cerr << "Image directory does not exist: " << directory << '\n';
        return 1;
    }

    const std::regex tileCoordinates(R"(_r([0-9]+)_c([0-9]+))",
                                     std::regex_constants::icase);
    std::vector<TileImage> tiles;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || !isImageFile(entry.path())) continue;

        std::smatch coordinateMatch;
        const std::string stem = entry.path().stem().string();
        if (!std::regex_search(stem, coordinateMatch, tileCoordinates)) {
            std::cout << "Ignoring image without _rNN_cNN coordinates: "
                      << entry.path().filename() << '\n';
            continue;
        }

        const int row = std::stoi(coordinateMatch[1].str());
        const int column = std::stoi(coordinateMatch[2].str());
        const std::string tileId =
            "r" + coordinateMatch[1].str() + "_c" + coordinateMatch[2].str();
        cv::Mat image = cv::imread(entry.path().string(), cv::IMREAD_COLOR);
        if (image.empty()) {
            std::cerr << "Could not read image: " << entry.path() << '\n';
            return 1;
        }
        tiles.push_back({entry.path(), tileId, row, column, std::move(image)});
    }

    if (tiles.size() < 2) {
        std::cerr << "Need at least two readable images named with _rNN_cNN "
                     "in directory " << directory << '\n';
        return 1;
    }

    std::sort(tiles.begin(), tiles.end(), [](const TileImage& left, const TileImage& right) {
        if (left.row != right.row) return left.row < right.row;
        if (left.column != right.column) return left.column < right.column;
        return left.path.filename() < right.path.filename();
    });

    std::map<std::pair<int, int>, std::size_t> tileByGridPosition;
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        const auto key = std::make_pair(tiles[index].row, tiles[index].column);
        if (!tileByGridPosition.emplace(key, index).second) {
            std::cerr << "Duplicate tile grid position _r" << tiles[index].row
                      << "_c" << tiles[index].column << ".\n";
            return 1;
        }
    }

    std::vector<TileOffset> offsets;
    std::size_t attemptedPairs = 0;
    for (std::size_t indexA = 0; indexA < tiles.size(); ++indexA) {
        const TileImage& tileA = tiles[indexA];
        const auto rightNeighbor = tileByGridPosition.find(
            {tileA.row, tileA.column + 1});
        const auto lowerNeighbor = tileByGridPosition.find(
            {tileA.row + 1, tileA.column});

        const auto processNeighbor = [&](std::size_t indexB,
                                         stitching::Direction direction) {
            const TileImage& tileB = tiles[indexB];
            PairResult result = matchPair(
                tileA.image, tileB.image, direction, overlap);
            ++attemptedPairs;
            std::cout << tileA.path.filename().string() << " -> "
                      << tileB.path.filename().string() << " ("
                      << (direction == stitching::Direction::LeftRight
                            ? "horizontal" : "vertical")
                      << "): " << result.matches.pointsA.size()
                      << " matches, dx=" << result.matches.dx
                      << ", dy=" << result.matches.dy
                      << (result.success ? " [OK]" : " [NO MATCH]") << '\n';
            if (result.success) {
                offsets.push_back({
                    indexA,
                    indexB,
                    cv::Point2d(result.matches.dx, result.matches.dy)
                });
            }
        };

        if (rightNeighbor != tileByGridPosition.end()) {
            processNeighbor(rightNeighbor->second, stitching::Direction::LeftRight);
        }
        if (lowerNeighbor != tileByGridPosition.end()) {
            processNeighbor(lowerNeighbor->second, stitching::Direction::BottomRight);
        }
    }

    if (attemptedPairs == 0) {
        std::cerr << "No horizontally or vertically adjacent tile pairs were found.\n";
        return 1;
    }
    if (offsets.empty()) {
        std::cerr << "SIFT did not find a usable match for any neighboring pair.\n";
        return 1;
    }

    std::vector<stitching::TilePositionData> tilePositions;
    if (!estimateTilePositions(tiles, offsets, tilePositions)) {
        std::cerr << "Could not calculate tile positions from the SIFT offsets.\n";
        return 1;
    }

    if (tilePositions.size() != tiles.size()) {
        std::cerr << "Warning: only " << tilePositions.size() << " of " << tiles.size()
                  << " tiles are connected to the reference tile by successful SIFT matches.\n";
    }
    std::cout << "\nProcessed " << attemptedPairs
              << " horizontal/vertical neighbor pairs. Reference tile "
              << tiles.front().tileId << " is anchored at (0, 0).\n";
    printPositions(tilePositions);
    return 0;
}

int runManualPair(const std::filesystem::path& pathA,
                  const std::filesystem::path& pathB,
                  const std::string& orientation,
                  float overlap) {
    if (orientation != "horizontal" && orientation != "vertical") {
        std::cerr << "Orientation must be 'horizontal' or 'vertical'.\n";
        return 2;
    }

    const cv::Mat imageA = cv::imread(pathA.string(), cv::IMREAD_COLOR);
    const cv::Mat imageB = cv::imread(pathB.string(), cv::IMREAD_COLOR);
    if (imageA.empty() || imageB.empty()) {
        std::cerr << "Could not read both images. Check the files and paths:\n"
                  << "  image A: " << pathA << '\n'
                  << "  image B: " << pathB << '\n';
        return 1;
    }

    const auto direction = orientation == "horizontal"
        ? stitching::Direction::LeftRight
        : stitching::Direction::BottomRight;
    PairResult result = matchPair(imageA, imageB, direction, overlap);
    std::cout << "Tiles: " << pathA.filename().string()
              << " -> " << pathB.filename().string() << '\n'
              << "Orientation: " << orientation << ", overlap: "
              << overlap * 100.f << "%\n"
              << "Correspondences: " << result.matches.pointsA.size() << '\n'
              << "Estimated translation (dx, dy): ("
              << result.matches.dx << ", " << result.matches.dy << ")\n"
              << "SIFT test " << (result.success ? "passed.\n" : "failed.\n");
    if (!result.success) return 1;

    std::vector<stitching::TilePositionData> tilePositions{
        {pathA.stem().string(), cv::Point2d(0.0, 0.0)},
        {pathB.stem().string(),
         cv::Point2d(result.matches.dx, result.matches.dy)}
    };
    printPositions(tilePositions);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 2 && std::filesystem::is_directory(argv[1])) {
            if (argc > 3) {
                std::cerr << "Usage: sift_matcher_test <image_directory> [overlap]\n";
                return 2;
            }
            float overlap = 0.3f;
            if (argc == 3 && !parseOverlap(argv[2], overlap)) {
                std::cerr << "Overlap must be a number between 0 and 1.\n";
                return 2;
            }
            return runDirectory(argv[1], overlap);
        }

        if (argc < 3 || argc > 5) {
            std::cerr << "Usage:\n"
                         "  sift_matcher_test <image_directory> [overlap]\n"
                         "  sift_matcher_test <image_a> <image_b> "
                         "[horizontal|vertical] [overlap]\n";
            return 2;
        }

        const std::string orientation = argc >= 4 ? argv[3] : "horizontal";
        float overlap = 0.3f;
        if (argc == 5 && !parseOverlap(argv[4], overlap)) {
            std::cerr << "Overlap must be a number between 0 and 1.\n";
            return 2;
        }
        return runManualPair(argv[1], argv[2], orientation, overlap);
    } catch (const std::exception& error) {
        std::cerr << "SIFT pairwise test failed: " << error.what() << '\n';
        return 1;
    }
}
