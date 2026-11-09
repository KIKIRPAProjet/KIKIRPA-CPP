// src/backend/steps/final_verification_step.cpp
#include "steps/final_verification_step.h"

#include "algorithms/verification/final_verification.h"
#include "core/checkpoint/final_verification_checkpoint.h"
#include "core/config/pipeline_config.h"
#include "core/step/data_store.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <exception>
#include <stdexcept>
#include <string>
#include <system_error>

namespace StepsNs {

    namespace fs = std::filesystem;

    FinalVerificationStepClass::FinalVerificationStepClass(const FinalVerificationInput& input) {
        CheckpointNs::registerFinalVerificationCheckpoint();
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "final-verification";
        stepConfig.parameters = {
            {"string", "inputImagePath", input.inputImagePath},
            {"string", "outputImagePath", input.outputImagePath}
        };
        stepConfig.outputs = {{output.type, output.key}};
        configJson = stepConfig;
    }

    bool FinalVerificationStepClass::execute() {
        output.value.details = Json::object();
        result = {{"status", "failed"}, {"error", ""}};
        if (dataStore) dataStore->replace(output.key, output.value);
        try {
            if (!dataStore) throw std::runtime_error("final-verification: no data store is attached");
            const fs::path inputImagePath = getParameter("inputImagePath").get<std::string>();
            const fs::path outputImagePath = getParameter("outputImagePath").get<std::string>();
            output.value.details = VerificationNs::compareImages(inputImagePath, outputImagePath);
            result = output.value.details;
            dataStore->replace(output.key, output.value);
            return true;
        } catch (const std::exception& exception) {
            result["error"] = exception.what();
            return false;
        }
    }

    Json FinalVerificationStepClass::externalInputsFingerprint() const {
        Json fingerprint = Json::array();
        for (const char* parameterName : {"inputImagePath", "outputImagePath"}) {
            const fs::path imagePath = getParameter(parameterName).get<std::string>();
            std::error_code errorCode;
            const bool exists = fs::is_regular_file(imagePath, errorCode);
            if (!exists || errorCode) {
                fingerprint.push_back({{"path", imagePath.string()}, {"exists", false}});
                continue;
            }
            const auto fileSize = fs::file_size(imagePath, errorCode);
            if (errorCode) {
                fingerprint.push_back({{"path", imagePath.string()}, {"exists", true}, {"sizeError", errorCode.message()}});
                continue;
            }
            const auto modifiedTime = fs::last_write_time(imagePath, errorCode);
            fingerprint.push_back({{"path", imagePath.string()}, {"exists", true}, {"size", fileSize},
                                   {"modified", errorCode ? std::string() : std::to_string(modifiedTime.time_since_epoch().count())}});
        }
        return fingerprint;
    }

}
