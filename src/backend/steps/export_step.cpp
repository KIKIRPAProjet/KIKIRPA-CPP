// src/backend/steps/export_step.cpp
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "steps/export_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/io/file_fingerprint.h"
#include "core/io/file_replacement.h"
#include "core/io/unicode_path.h"

#include <nlohmann/json.hpp>
#include <opencv2/core/types.hpp>
#include <tiff.h>
#include <tiffio.h>

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace StepsNs {

    namespace fs = std::filesystem;

    namespace {

        // Format attendu de l'image de la mosaïque : RGB, 16 bits par canal, tuilée.
        constexpr std::uint16_t EXPECTED_SAMPLES_PER_PIXEL = 3;
        constexpr std::uint16_t EXPECTED_BITS_PER_SAMPLE = 16;

        // Ce que l'en-tête du TIFF de la mosaïque dit de l'image, lu sans décoder de pixel.
        struct TiffHeader {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint16_t samplesPerPixel = 0;
            std::uint16_t bitsPerSample = 0;
            bool isTiled = false;
            bool isBigTiff = false;
        };

        fs::path downloadsDirectory() {
#ifdef _WIN32
            PWSTR knownFolderPath = nullptr;
            const HRESULT result = SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &knownFolderPath);
            if (SUCCEEDED(result) && knownFolderPath) {
                const fs::path path(knownFolderPath);
                CoTaskMemFree(knownFolderPath);
                return path;
            }
            if (knownFolderPath) CoTaskMemFree(knownFolderPath);
            const wchar_t* profilePath = _wgetenv(L"USERPROFILE");
            if (profilePath && *profilePath) return fs::path(profilePath) / L"Downloads";
#else
            const char* homePath = std::getenv("HOME");
            if (homePath && *homePath) return fs::path(homePath) / "Downloads";
#endif
            throw std::runtime_error("cannot resolve the current user's Downloads directory");
        }

        fs::path configuredOutputDirectory(const Json& configJson) {
            std::string directory;
            if (configJson.contains("parameters") && configJson["parameters"].is_array()) {
                for (const Json& parameter : configJson.at("parameters")) {
                    if (parameter.value("name", "") == "outputDirectory" && parameter.contains("value") &&
                        parameter["value"].is_string()) {
                        directory = parameter["value"].get<std::string>();
                        break;
                    }
                }
            }
            return directory.empty() ? downloadsDirectory() : IONs::pathFromUtf8(directory);
        }

        std::string configuredFilename(const Json& configJson) {
            std::string filename;
            if (configJson.contains("parameters") && configJson["parameters"].is_array()) {
                for (const Json& parameter : configJson.at("parameters")) {
                    if (parameter.value("name", "") == "filename" && parameter.contains("value") &&
                        parameter["value"].is_string()) {
                        filename = parameter["value"].get<std::string>();
                        break;
                    }
                }
            }
            if (filename.empty()) {
                return "mosaic.tif";
            }
            const fs::path filenamePath = IONs::pathFromUtf8(filename).filename();
            std::string cleanName = IONs::pathToUtf8(filenamePath);
            if (filenamePath.extension().empty()) {
                cleanName += ".tif";
            }
            return cleanName.empty() ? "mosaic.tif" : cleanName;
        }

        fs::path generateUniquePath(const fs::path& directory, const std::string& filename) {
            const fs::path target = directory / IONs::pathFromUtf8(filename);
            if (!fs::exists(target)) {
                return target;
            }
            const std::string stem = IONs::pathToUtf8(target.stem());
            const std::string extension = IONs::pathToUtf8(target.extension());
            std::uint64_t counter = 1;
            while (true) {
                const fs::path candidate =
                    directory / IONs::pathFromUtf8(stem + " (" + std::to_string(counter) + ")" + extension);
                if (!fs::exists(candidate)) {
                    return candidate;
                }
                ++counter;
            }
        }

        fs::path findLatestExistingPath(const fs::path& directory, const std::string& filename) {
            const fs::path target = directory / IONs::pathFromUtf8(filename);
            if (!fs::exists(target)) {
                return target;
            }
            const std::string stem = IONs::pathToUtf8(target.stem());
            const std::string extension = IONs::pathToUtf8(target.extension());
            fs::path latest = target;
            std::uint64_t counter = 1;
            while (true) {
                const fs::path candidate =
                    directory / IONs::pathFromUtf8(stem + " (" + std::to_string(counter) + ")" + extension);
                if (!fs::exists(candidate)) {
                    break;
                }
                latest = candidate;
                ++counter;
            }
            return latest;
        }

        bool readTiffHeader(const fs::path& path, TiffHeader& header, std::string& errorMessage) {
            TIFF* rawTiffFile = IONs::openTiff(path, "r");
            if (!rawTiffFile) {
                errorMessage = "cannot open mosaic image '" + IONs::pathToUtf8(path) + "'";
                return false;
            }
            const std::unique_ptr<TIFF, decltype(&TIFFClose)> tiffFile(rawTiffFile, &TIFFClose);
            if (!TIFFGetField(tiffFile.get(), TIFFTAG_IMAGEWIDTH, &header.width) ||
                !TIFFGetField(tiffFile.get(), TIFFTAG_IMAGELENGTH, &header.height) ||
                !TIFFGetFieldDefaulted(tiffFile.get(), TIFFTAG_SAMPLESPERPIXEL, &header.samplesPerPixel) ||
                !TIFFGetFieldDefaulted(tiffFile.get(), TIFFTAG_BITSPERSAMPLE, &header.bitsPerSample)) {
                errorMessage = "cannot read the header of mosaic image '" + IONs::pathToUtf8(path) + "'";
                return false;
            }
            header.isTiled = TIFFIsTiled(tiffFile.get()) != 0;
            header.isBigTiff = TIFFIsBigTIFF(tiffFile.get()) != 0;
            return true;
        }

        // Contrôle que le fichier annoncé par l'assemblage est bien l'image décrite (dimensions et format), avant
        // de le publier.
        bool validateMosaicImage(const DataNs::Mosaic& mosaic, TiffHeader& header, std::string& errorMessage) {
            if (mosaic.canvasSize.width <= 0 || mosaic.canvasSize.height <= 0) {
                errorMessage = "mosaic canvas size must be positive";
                return false;
            }
            if (!readTiffHeader(mosaic.imagePath, header, errorMessage)) return false;
            if (header.width != static_cast<std::uint32_t>(mosaic.canvasSize.width) ||
                header.height != static_cast<std::uint32_t>(mosaic.canvasSize.height)) {
                errorMessage = "mosaic image dimensions do not match the mosaic canvas size";
                return false;
            }
            if (header.samplesPerPixel != EXPECTED_SAMPLES_PER_PIXEL ||
                header.bitsPerSample != EXPECTED_BITS_PER_SAMPLE || !header.isTiled) {
                errorMessage = "mosaic image must be a tiled 16-bit RGB TIFF";
                return false;
            }
            return true;
        }

        bool prepareOutputDirectory(const Json& config, fs::path& directory, Json& result) {
            directory = configuredOutputDirectory(config);
            std::error_code directoryError;
            fs::create_directories(directory, directoryError);
            if (!directoryError) return true;
            result["error"] =
                "cannot create output directory '" + IONs::pathToUtf8(directory) + "': " + directoryError.message();
            return false;
        }

        bool exportMosaic(const DataNs::Mosaic& mosaic, const Json& config, ExportOutput& output, Json& result) {
            TiffHeader header;
            std::string errorMessage;
            if (!validateMosaicImage(mosaic, header, errorMessage)) {
                result["error"] = errorMessage;
                return false;
            }
            fs::path directory;
            if (!prepareOutputDirectory(config, directory, result)) return false;
            const std::string filename = configuredFilename(config);
            const fs::path imagePath = generateUniquePath(directory, filename);
            if (!IONs::copyFileReplacing(mosaic.imagePath, imagePath, errorMessage)) {
                result["error"] = errorMessage;
                return false;
            }
            output = {imagePath, mosaic.canvasSize, header.isBigTiff};
            result["image"] = {{"path", IONs::pathToUtf8(imagePath)}, {"bigTiff", output.isBigTiff}};
            result["canvasSize"] = {output.canvasSize.width, output.canvasSize.height};
            return true;
        }

    }

    ExportStepClass::ExportStepClass(const ExportInput& initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "export";
        stepConfig.parameters = {{"string", "outputDirectory", initialInput.outputDirectory},
                                 {"string", "filename", initialInput.filename}};
        stepConfig.inputs = {{"Mosaic", "mosaic"}};
        configJson = stepConfig;
    }

    Json ExportStepClass::externalInputsFingerprint() const {
        const fs::path targetPath = output.imagePath.empty()
            ? findLatestExistingPath(configuredOutputDirectory(configJson), configuredFilename(configJson))
            : output.imagePath;
        return IONs::computeFileFingerprint(targetPath);
    }

    bool ExportStepClass::execute() {
        result = Json::object();
        output = {};

        try {
            if (!dataStore) throw std::runtime_error("data store is not available");
            const auto& mosaic = dataStore->get<DataNs::Mosaic>("mosaic");
            return exportMosaic(mosaic, configJson, output, result);
        } catch (const std::exception& exception) {
            result["error"] = exception.what();
            return false;
        }
    }

}
