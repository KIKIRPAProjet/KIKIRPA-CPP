// src/backend/steps/mosaic_crop_step.cpp
#include "steps/mosaic_crop_step.h"

#include "algorithms/cropping/mosaic_crop.h"
#include "core/config/pipeline_config.h"
#include "core/data/mosaic_files.h"
#include "core/step/data_store.h"

#include <nlohmann/json.hpp>
#include <opencv2/core/types.hpp>

#include <exception>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

namespace StepsNs {

    using Json = nlohmann::json;

    namespace {

        // get<int>() seul peut tronquer une grande valeur JSON.
        int readCoordinate(const Json& value, const std::string& name) {
            const int maximum = std::numeric_limits<int>::max();
            const int minimum = std::numeric_limits<int>::min();
            const bool isOutside = value.is_number_unsigned() ?
                value.get<unsigned long long>() > static_cast<unsigned long long>(maximum) :
                value.get<long long>() < minimum || value.get<long long>() > maximum;

            if (isOutside) {
                throw std::invalid_argument(name + " exceeds the supported integer range");
            }

            return value.get<int>();
        }

    }

    MosaicCropStepClass::MosaicCropStepClass(const MosaicCropInput& input) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "mosaic-crop";
        stepConfig.parameters = {
            {"int", "x", input.x},
            {"int", "y", input.y},
            {"int", "width", input.width},
            {"int", "height", input.height},
            {"string", "outputDirectory", input.outputDirectory}
        };
        stepConfig.inputs = { {"MosaicFiles", output.key} };
        stepConfig.outputs = { {output.type, output.key} };
        configJson = stepConfig;
    }

    void MosaicCropStepClass::cropAndPublish() {
        CroppingNs::CropInput request;
        request.mosaic = dataStore->get<DataNs::MosaicFiles>(output.key);
        request.rectangle = cv::Rect(
            readCoordinate(getParameter("x"), "x"),
            readCoordinate(getParameter("y"), "y"),
            readCoordinate(getParameter("width"), "width"),
            readCoordinate(getParameter("height"), "height"));
        request.outputDirectory = std::filesystem::u8path(
            getParameter("outputDirectory").get<std::string>());

        std::string errorMessage;
        if (!CroppingNs::cropMosaic(request, output.value, errorMessage)) {
            throw std::runtime_error(errorMessage);
        }

        dataStore->replace(output.key, output.value);
        result["mosaicPath"] = output.value.mosaicPath;
        result["totalBlocks"] = output.value.totalBlocks;
    }

    bool MosaicCropStepClass::execute() {
        output = {};
        result = { {"error", ""}, {"totalBlocks", 0} };

        try {
            if (!dataStore) throw std::runtime_error("DataStore is not attached");
            cropAndPublish();
            return true;
        }
        catch (const std::exception& exception) {
            output.value = {};
            result["error"] = "mosaic-crop: " + std::string(exception.what());
            return false;
        }
    }

}