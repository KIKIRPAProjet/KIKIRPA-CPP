
#include "steps/RansacStep.h"

#include "core/Config.h"
#include "algorithms/rejection/ransac/RansacRejector.h"

#include <exception>
#include <string>

namespace stitching::steps {

namespace {

template <typename T>
T readOption(
    const Json& config,
    const std::string& key,
    const T& defaultValue
) {
    if (!config.contains("options") ||
        !config["options"].is_array()) {
        return defaultValue;
    }

    for (const auto& item : config["options"]) {
        if (item.value("key", std::string{}) == key &&
            item.contains("value")) {
            return item["value"].get<T>();
        }
    }

    return defaultValue;
}

} // namespace

RansacStep::RansacStep(const Json& params) {
    StepConfig cfg;

    cfg.name = "ransac";

    cfg.options = {
        {"int", "minMatches",
            params.value("minMatches", 4)},
        {"double", "ransacReprojThreshold",
            params.value("ransacReprojThreshold", 3.0)},
        {"int", "maxIters",
            params.value("maxIters", 2000)},
        {"double", "confidence",
            params.value("confidence", 0.99)},
        {"int", "refineIters",
            params.value("refineIters", 10)},
        {"int", "minInliers",
            params.value("minInliers", 6)}
    };

    // Input received from the previous pipeline step.
    cfg.pipelineOptions = {
        {"Matches", "matches"}
    };

    // Output made available to subsequent steps.
    cfg.signature.pipelineOptions = {
        {"Alignment", "alignment"}
    };

    config = cfg;
}

bool RansacStep::execute() {
    outputData = Json::object();

    if (data == nullptr) {
        outputData["error"] = "DataStore is not connected";
        return false;
    }

    try {
        // 1. Read matched points from the shared DataStore.
        const Matches& matches =
            data->get<Matches>("matches");

        // 2. Read the configured RANSAC parameters.
        Json params;

        params["minMatches"] =
            readOption(config, "minMatches", 4);

        params["ransacReprojThreshold"] =
            readOption(config, "ransacReprojThreshold", 3.0);

        params["maxIters"] =
            readOption(config, "maxIters", 2000);

        params["confidence"] =
            readOption(config, "confidence", 0.99);

        params["refineIters"] =
            readOption(config, "refineIters", 10);

        params["minInliers"] =
            readOption(config, "minInliers", 6);

        // 3. Run the existing RANSAC implementation.
        stitching::rejection::RansacRejector rejector(params);

        Alignment alignment;

        if (!rejector.estimate(matches, alignment)) {
            outputData["error"] =
                "RANSAC could not estimate a valid alignment";
            return false;
        }

        // 4. Publish the result for subsequent pipeline steps.
        data->set("alignment", alignment);

        // 5. Report execution statistics.
        int inlierCount = 0;

        for (const auto inlier : alignment.inliers) {
            if (inlier != 0) {
                ++inlierCount;
            }
        }

        outputData["matches"] = matches.pointsA.size();
        outputData["inliers"] = inlierCount;
        outputData["outliers"] =
            static_cast<int>(alignment.inliers.size()) - inlierCount;

        return true;
    }
    catch (const std::exception& e) {
        outputData["error"] = e.what();
        return false;
    }
}

} // namespace stitching::steps
