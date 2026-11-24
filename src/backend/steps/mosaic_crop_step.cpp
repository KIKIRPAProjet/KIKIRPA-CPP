// src/backend/steps/mosaic_crop_step.cpp
#include "steps/mosaic_crop_step.h"

#include "algorithms/cropping/mosaic_crop.h"
#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/io/file_fingerprint.h"
#include "core/io/unicode_path.h"
#include "core/step/data_store.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
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
            if (!value.is_number_integer()) throw std::invalid_argument(name + " must be an integer");
            const int maximum = std::numeric_limits<int>::max();
            const int minimum = std::numeric_limits<int>::min();
            const bool isOutside = value.is_number_unsigned() ?
                value.get<unsigned long long>() > static_cast<unsigned long long>(maximum) :
                value.get<long long>() < minimum || value.get<long long>() > maximum;
            if (isOutside) throw std::invalid_argument(name + " exceeds the supported integer range");
            return value.get<int>();
        }

    }

    MosaicCropStepClass::MosaicCropStepClass(const MosaicCropInput& input) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "mosaic-crop";
        stepConfig.parameters = { {"int", "x", input.x}, {"int", "y", input.y},
                                 {"int", "width", input.width}, {"int", "height", input.height},
                                 {"string", "outputDirectory", input.outputDirectory} };
        stepConfig.inputs = { {"Mosaic", output.key} };
        stepConfig.outputs = { {output.type, output.key} };
        configJson = stepConfig;
    }

    Json MosaicCropStepClass::externalInputsFingerprint() const {
        const auto directory = IONs::pathFromUtf8(getParameter("outputDirectory").get<std::string>());
        if (directory.empty()) return Json();
        return Json::array({ IONs::computeFileFingerprint(directory / "mosaic_image.tif"),
                            IONs::computeFileFingerprint(directory / "mosaic_provenance.tif"),
                            IONs::computeFileFingerprint(directory / "mosaic_mask.tif") });
    }

    bool MosaicCropStepClass::cropAndPublish() {
        CroppingNs::CropInput request;
        request.mosaic = dataStore->get<DataNs::Mosaic>(output.key);
        request.rectangle = cv::Rect(readCoordinate(getParameter("x"), "x"),
            readCoordinate(getParameter("y"), "y"),
            readCoordinate(getParameter("width"), "width"),
            readCoordinate(getParameter("height"), "height"));
        request.outputDirectory = IONs::pathFromUtf8(getParameter("outputDirectory").get<std::string>());
        std::string errorMessage;
        if (!CroppingNs::cropMosaic(request, output.value, errorMessage)) {
            result["error"] = "mosaic-crop: " + errorMessage;
            return false;
        }
        dataStore->replace(output.key, output.value);
        result["image"] = IONs::pathToUtf8(output.value.imagePath);
        result["canvasSize"] = { output.value.canvasSize.width, output.value.canvasSize.height };
        return true;
    }

    bool MosaicCropStepClass::execute() {
        output = {};
        result = Json::object();
        try {
            if (!dataStore) {
                result["error"] = "mosaic-crop: data store is not attached";
                return false;
            }
            return cropAndPublish();
        }
        catch (const std::exception& exception) {
            output.value = {};
            result["error"] = "mosaic-crop: " + std::string(exception.what());
            return false;
        }
    }

}