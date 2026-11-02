#include "steps/FusionStep.h"
#include "core/Config.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace stitching::steps {

FusionStep::FusionStep(const FusionInput& input) {
    StepConfig cfg;
    cfg.name = "fusion";
    cfg.options = {{"string", "blending", input.blending}};
    cfg.pipelineOptions = {{"Tiles", keys::Tiles},
                           {"Positions", keys::Positions},
                           {"LightCorrections", keys::Corrections},
                           {"Territories", keys::Territories}};
    cfg.signature.pipelineOptions = {{"Mosaic", keys::Mosaic}};
    config = cfg;
}

bool FusionStep::execute() {
    const Tiles& tiles = data->get<Tiles>(keys::Tiles);
    const Positions& positions = data->get<Positions>(keys::Positions);
    const LightCorrections& corrections = data->get<LightCorrections>(keys::Corrections);
    const Territories& territories = data->get<Territories>(keys::Territories);
    const std::string blending = option("blending").get<std::string>();
    if (blending != "average") {
        outputData["error"] = "blending currently supports only 'average'";
        return false;
    }
    if (tiles.empty() || positions.size() != tiles.size() ||
        corrections.size() != tiles.size() || territories.size() != tiles.size()) {
        outputData["error"] = "tiles, positions, corrections and territories must have equal non-zero counts";
        return false;
    }

    const int channels = tiles.front().image.channels();
    if (channels != 1 && channels != 3 && channels != 4) {
        outputData["error"] = "mosaic supports 1, 3 or 4 channel images";
        return false;
    }
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const cv::Mat& image = tiles[i].image;
        if (image.empty() || image.channels() != channels ||
            image.depth() != tiles.front().image.depth()) {
            outputData["error"] = "all tile images must be non-empty and have the same channel count and depth";
            return false;
        }
        if (positions[i].tile != i || territories[i].tile != i ||
            territories[i].mask.size() != image.size()) {
            outputData["error"] = "position or territory does not match its tile";
            return false;
        }
        const cv::Point2f corners[] = {
            {0.f, 0.f}, {static_cast<float>(image.cols), 0.f},
            {static_cast<float>(image.cols), static_cast<float>(image.rows)},
            {0.f, static_cast<float>(image.rows)}
        };
        std::vector<cv::Point2f> transformed;
        cv::perspectiveTransform(std::vector<cv::Point2f>(corners, corners + 4), transformed,
                                 cv::Mat(positions[i].tileToWorld));
        for (const cv::Point2f& point : transformed) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                outputData["error"] = "tile position contains non-finite coordinates";
                return false;
            }
            minX = std::min(minX, static_cast<double>(point.x));
            minY = std::min(minY, static_cast<double>(point.y));
            maxX = std::max(maxX, static_cast<double>(point.x));
            maxY = std::max(maxY, static_cast<double>(point.y));
        }
    }

    const double left = std::floor(minX);
    const double top = std::floor(minY);
    const double right = std::ceil(maxX);
    const double bottom = std::ceil(maxY);
    if (right <= left || bottom <= top ||
        right - left > std::numeric_limits<int>::max() ||
        bottom - top > std::numeric_limits<int>::max()) {
        outputData["error"] = "computed mosaic dimensions are invalid or too large";
        return false;
    }
    const cv::Size size(static_cast<int>(right - left), static_cast<int>(bottom - top));
    const cv::Matx33d offset(1, 0, -left, 0, 1, -top, 0, 0, 1);
    cv::Mat accumulated = cv::Mat::zeros(size, CV_MAKETYPE(CV_32F, channels));
    cv::Mat weights = cv::Mat::zeros(size, CV_32F);
    std::vector<MosaicBlock> blocks;
    blocks.reserve(tiles.size());

    for (std::size_t i = 0; i < tiles.size(); ++i) {
        cv::Mat source;
        tiles[i].image.convertTo(source, CV_MAKETYPE(CV_32F, channels));
        std::vector<cv::Mat> planes;
        cv::split(source, planes);
        for (int c = 0; c < channels; ++c) {
            planes[c] = planes[c] * corrections[i].gain[c] + corrections[i].bias[c];
        }
        cv::merge(planes, source);

        cv::Mat warped;
        const cv::Matx33d transform = offset * positions[i].tileToWorld;
        cv::warpPerspective(source, warped, cv::Mat(transform), size, cv::INTER_LINEAR,
                            cv::BORDER_CONSTANT, cv::Scalar::all(0));
        cv::Mat mask;
        cv::warpPerspective(territories[i].mask, mask, cv::Mat(transform), size, cv::INTER_NEAREST,
                            cv::BORDER_CONSTANT, cv::Scalar::all(0));
        cv::Mat weight;
        mask.convertTo(weight, CV_32F, 1.0 / 255.0);
        cv::Mat expandedWeight;
        cv::merge(std::vector<cv::Mat>(channels, weight), expandedWeight);
        cv::multiply(warped, expandedWeight, warped);
        cv::add(accumulated, warped, accumulated);
        cv::add(weights, weight, weights);

        std::vector<cv::Point2f> corners = {
            {0.f, 0.f}, {static_cast<float>(tiles[i].image.cols), 0.f},
            {static_cast<float>(tiles[i].image.cols), static_cast<float>(tiles[i].image.rows)},
            {0.f, static_cast<float>(tiles[i].image.rows)}
        };
        std::vector<cv::Point2f> transformed;
        cv::perspectiveTransform(corners, transformed, cv::Mat(transform));
        cv::Rect bounds = cv::boundingRect(transformed) & cv::Rect(0, 0, size.width, size.height);
        blocks.push_back({bounds, warped(bounds).clone()});
    }

    cv::Mat expandedWeights;
    cv::merge(std::vector<cv::Mat>(channels, weights), expandedWeights);
    cv::Mat normalized;
    cv::divide(accumulated, expandedWeights, normalized, 1.0, CV_MAKETYPE(CV_32F, channels));
    cv::Mat valid;
    cv::compare(weights, 0, valid, cv::CMP_GT);
    normalized.setTo(cv::Scalar::all(0), ~valid);

    Mosaic mosaic{std::move(normalized), std::move(blocks), tiles.front().image.depth()};
    data->set(keys::Mosaic, std::move(mosaic));
    outputData["width"] = size.width;
    outputData["height"] = size.height;
    return true;
}

} // namespace stitching::steps
