#include "steps/StitchingStep.h"
#include "core/Config.h"
#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>

namespace stitching::steps {

StitchingStep::StitchingStep(const StitchingInput& input) {
    StepConfig cfg;
    cfg.name = "stitching";
    cfg.options = {{"string", "matcher", input.matcher},
                   {"string", "rejector", input.rejector},
                   {"int", "level", input.level},
                   {"float", "ratio", input.ratio},
                   {"float", "threshold", input.threshold}};
    cfg.pipelineOptions = {{"Tiles", keys::Tiles}};
    cfg.signature.pipelineOptions = {{"Matches", keys::Matches},
                                     {"Alignments", keys::Alignments}};
    config = cfg;
}

bool StitchingStep::execute() {
        const Tiles& tiles = data->get<Tiles>(keys::Tiles);
    const std::string matcherName = option("matcher").get<std::string>();
    const std::string rejectorName = option("rejector").get<std::string>();
    const int level = option("level").get<int>();
    const float ratio = option("ratio").get<float>();
    const double threshold = option("threshold").get<double>();
    if (matcherName != "sift" && matcherName != "orb") {
        outputData["error"] = "matcher must be 'sift' or 'orb'";
        return false;
    }
    if (rejectorName != "ransac") {
        outputData["error"] = "rejector must be 'ransac'";
        return false;
    }
    if (level < 0 || level > 5 || ratio <= 0.f || ratio >= 1.f || threshold <= 0.0) {
        outputData["error"] = "level must be 0..5, ratio must be in (0, 1), and threshold must be positive";
        return false;
    }

    std::vector<cv::Mat> images;
    std::vector<std::vector<cv::KeyPoint>> keypoints(tiles.size());
    std::vector<cv::Mat> descriptors(tiles.size());
    const int scale = 1 << level;
    for (const TileImage& tile : tiles) {
        if (tile.image.empty()) {
            outputData["error"] = "tile image is empty: " + tile.path.string();
            return false;
        }
        cv::Mat gray;
        if (tile.image.channels() == 1) gray = tile.image;
        else if (tile.image.channels() == 3)
            cv::cvtColor(tile.image, gray, cv::COLOR_BGR2GRAY);
        else if (tile.image.channels() == 4)
            cv::cvtColor(tile.image, gray, cv::COLOR_BGRA2GRAY);
        else {
            outputData["error"] = "unsupported channel count in " + tile.path.string();
            return false;
        }
        if (gray.depth() == CV_16U) {
            gray.convertTo(gray, CV_8U, 1.0 / 257.0);
        } else if (gray.depth() != CV_8U) {
            outputData["error"] = "feature matching supports only 8-bit or 16-bit tiles: " +
                                  tile.path.string();
            return false;
        }
        if (gray.cols < scale || gray.rows < scale) {
            outputData["error"] = "tile is too small for the selected level: " + tile.path.string();
            return false;
        }
        if (level > 0)
            cv::resize(gray, gray, cv::Size(), 1.0 / scale, 1.0 / scale, cv::INTER_AREA);
        images.push_back(std::move(gray));
    }

    cv::Ptr<cv::Feature2D> detector = matcherName == "sift"
        ? cv::SIFT::create()
        : cv::ORB::create(5000);
    for (std::size_t i = 0; i < images.size(); ++i)
        detector->detectAndCompute(images[i], cv::noArray(), keypoints[i], descriptors[i]);

    Matches allMatches;
    Alignments allAlignments;
    for (std::size_t a = 0; a < tiles.size(); ++a) {
        for (std::size_t b = a + 1; b < tiles.size(); ++b) {
            const int dx = tiles[b].col - tiles[a].col;
            const int dy = tiles[b].row - tiles[a].row;
            if (!((dx == 1 && dy == 0) || (dx == 0 && dy == 1))) continue;
            const Direction direction = dx == 1 ? Direction::Right : Direction::Below;
            if (descriptors[a].empty() || descriptors[b].empty()) {
                outputData["error"] = "no features found for adjacent tiles " +
                                      std::to_string(a) + " and " + std::to_string(b);
                return false;
            }

            cv::BFMatcher bf(matcherName == "sift" ? cv::NORM_L2 : cv::NORM_HAMMING);
            std::vector<std::vector<cv::DMatch>> candidates;
            bf.knnMatch(descriptors[b], descriptors[a], candidates, 2);
            std::vector<cv::Point2f> firstPoints;
            std::vector<cv::Point2f> secondPoints;
            for (const auto& pair : candidates) {
                if (pair.size() < 2 || pair[0].distance >= ratio * pair[1].distance) continue;
                cv::Point2f p1 = keypoints[a][pair[0].trainIdx].pt;
                cv::Point2f p2 = keypoints[b][pair[0].queryIdx].pt;
                p1 *= static_cast<float>(scale);
                p2 *= static_cast<float>(scale);
                firstPoints.push_back(p1);
                secondPoints.push_back(p2);
            }
            if (firstPoints.size() < 4) {
                outputData["error"] = "fewer than four matched features for adjacent tiles " +
                                      std::to_string(a) + " and " + std::to_string(b);
                return false;
            }

            cv::Mat inlierMask;
            const cv::Mat transform = cv::findHomography(secondPoints, firstPoints, cv::RANSAC,
                                                          threshold, inlierMask);
            if (transform.empty()) {
                outputData["error"] = "RANSAC could not align adjacent tiles " +
                                      std::to_string(a) + " and " + std::to_string(b);
                return false;
            }
            cv::Mat transform64;
            transform.convertTo(transform64, CV_64F);
            cv::Matx33d matrix;
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                    matrix(row, col) = transform64.at<double>(row, col);

            PairMatches pairMatches{{a, b, direction}, std::move(firstPoints),
                                    std::move(secondPoints)};
            PairAlignment alignment;
            alignment.pair = pairMatches.pair;
            alignment.secondToFirst = matrix;
            alignment.inliers.assign(inlierMask.datastart, inlierMask.dataend);
            allMatches.push_back(std::move(pairMatches));
            allAlignments.push_back(std::move(alignment));
        }
    }
    if (allAlignments.empty()) {
        outputData["error"] = "no adjacent tile pairs were found";
        return false;
    }

    data->set(keys::Matches, std::move(allMatches));
    data->set(keys::Alignments, std::move(allAlignments));
    outputData["pairs"] = data->get<Alignments>(keys::Alignments).size();
    return true;
}

} // namespace stitching::steps
