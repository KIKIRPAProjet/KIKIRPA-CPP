// src/backend/steps/export_step.cpp
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "steps/export_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/io/file_replacement.h"

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

        fs::path makeImagePath(const fs::path& directory) { return directory / "mosaic.tif"; }

        Json outputFingerprint(const fs::path& path) {
            std::error_code errorCode;
            const bool exists = fs::exists(path, errorCode);
            if (errorCode) throw fs::filesystem_error("cannot inspect export output", path, errorCode);
            if (!exists) {
                return {{"path", path.u8string()}, {"exists", false}};
            }
            const std::uintmax_t fileSize = fs::file_size(path, errorCode);
            if (errorCode) throw fs::filesystem_error("cannot read export output size", path, errorCode);
            const auto modifiedTime = fs::last_write_time(path, errorCode);
            if (errorCode) throw fs::filesystem_error("cannot read export output time", path, errorCode);
            return {{"path", path.u8string()},
                    {"exists", true},
                    {"size", fileSize},
                    {"modified", modifiedTime.time_since_epoch().count()}};
        }

        fs::path configuredOutputDirectory(const Json& configJson) {
            std::string directory;
            for (const Json& parameter : configJson.at("parameters")) {
                if (parameter.at("name") == "outputDirectory") {
                    directory = parameter.at("value").get<std::string>();
                    break;
                }
            }
            return directory.empty() ? downloadsDirectory() : fs::u8path(directory);
        }

        bool readTiffHeader(const fs::path& path, TiffHeader& header, std::string& errorMessage) {
            TIFF* rawTiffFile = TIFFOpen(path.string().c_str(), "r");
            if (!rawTiffFile) {
                errorMessage = "cannot open mosaic image '" + path.string() + "'";
                return false;
            }
            const std::unique_ptr<TIFF, decltype(&TIFFClose)> tiffFile(rawTiffFile, &TIFFClose);
            if (!TIFFGetField(tiffFile.get(), TIFFTAG_IMAGEWIDTH, &header.width) ||
                !TIFFGetField(tiffFile.get(), TIFFTAG_IMAGELENGTH, &header.height) ||
                !TIFFGetFieldDefaulted(tiffFile.get(), TIFFTAG_SAMPLESPERPIXEL, &header.samplesPerPixel) ||
                !TIFFGetFieldDefaulted(tiffFile.get(), TIFFTAG_BITSPERSAMPLE, &header.bitsPerSample)) {
                errorMessage = "cannot read the header of mosaic image '" + path.string() + "'";
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
                "cannot create output directory '" + directory.string() + "': " + directoryError.message();
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
            const fs::path imagePath = makeImagePath(directory);
            if (!IONs::copyFileReplacing(mosaic.imagePath, imagePath, errorMessage)) {
                result["error"] = errorMessage;
                return false;
            }
            output = {imagePath, mosaic.canvasSize, header.isBigTiff};
            result["image"] = {{"path", imagePath.u8string()}, {"bigTiff", output.isBigTiff}};
            result["canvasSize"] = {output.canvasSize.width, output.canvasSize.height};
            return true;
        }

    }

    ExportStepClass::ExportStepClass(const ExportInput& initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "export";
        stepConfig.parameters = {{"string", "outputDirectory", initialInput.outputDirectory}};
        stepConfig.inputs = {{"Mosaic", "mosaic"}};
        configJson = stepConfig;
    }

    Json ExportStepClass::externalInputsFingerprint() const {
        return outputFingerprint(makeImagePath(configuredOutputDirectory(configJson)));
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
