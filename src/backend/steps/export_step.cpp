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
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/unicode_path.h"

#include <nlohmann/json.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgproc.hpp>
#include <tiff.h>
#include <tiffio.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
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
        constexpr double SIXTEEN_TO_EIGHT_BIT_DIVISOR = 257.0;

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

        std::string configuredFormat(const Json& configJson) {
            if (!configJson.contains("parameters") || !configJson["parameters"].is_array()) return "tiff";
            for (const Json& parameter : configJson.at("parameters")) {
                if (parameter.value("name", "") == "format" && parameter.contains("value") &&
                    parameter["value"].is_string()) {
                    return parameter["value"].get<std::string>();
                }
            }
            return "tiff";
        }

        fs::path configuredOutputDirectory(const Json& configJson) {
            if (!configJson.contains("parameters") || !configJson["parameters"].is_array()) {
                return downloadsDirectory();
            }
            for (const Json& parameter : configJson.at("parameters")) {
                if (parameter.value("name", "") == "outputDirectory" && parameter.contains("value") &&
                    parameter["value"].is_string()) {
                    const std::string directory = parameter["value"].get<std::string>();
                    return directory.empty() ? downloadsDirectory() : IONs::pathFromUtf8(directory);
                }
            }
            return downloadsDirectory();
        }

        std::string sanitizeFilename(const std::string& filename, const std::string& format) {
            const std::string extension = format == "jpeg" ? ".jpg" : ".tif";
            if (filename.empty()) return format == "jpeg" ? "mosaic.jpg" : "mosaic.tif";
            const fs::path filenamePath = IONs::pathFromUtf8(filename).filename();
            std::string stem = IONs::pathToUtf8(filenamePath.stem());
            if (stem.empty()) stem = format == "jpeg" ? "mosaic" : "mosaic";
            return stem + extension;
        }

        std::string configuredFilename(const Json& configJson) {
            const std::string format = configuredFormat(configJson);
            if (!configJson.contains("parameters") || !configJson["parameters"].is_array()) {
                return sanitizeFilename("", format);
            }
            for (const Json& parameter : configJson.at("parameters")) {
                if (parameter.value("name", "") == "filename" && parameter.contains("value") &&
                    parameter["value"].is_string()) {
                    return sanitizeFilename(parameter["value"].get<std::string>(), format);
                }
            }
            return sanitizeFilename("", format);
        }

        // Copies numérotées d'un même export : <nom><ext> est le rang 0, « <nom> (1)<ext> » le rang 1, etc. Un nouvel
        // export prend le rang qui suit le plus élevé présent : le dernier export est toujours celui de rang maximal,
        // même si l'utilisateur a supprimé des copies intermédiaires.
        struct ExportName {
            fs::path directory;
            std::string stem;
            std::string extension;
        };

        constexpr std::size_t MAXIMUM_RANK_DIGITS = 18;   // tient dans un std::uint64_t

        ExportName splitExportName(const fs::path& directory, const std::string& filename) {
            const fs::path target = directory / IONs::pathFromUtf8(filename);
            return {directory, IONs::pathToUtf8(target.stem()), IONs::pathToUtf8(target.extension())};
        }

        fs::path exportPath(const ExportName& name, std::uint64_t rank) {
            if (rank == 0) return name.directory / IONs::pathFromUtf8(name.stem + name.extension);
            return name.directory / IONs::pathFromUtf8(name.stem + " (" + std::to_string(rank) + ")" + name.extension);
        }

        // Rang d'une copie numérotée « <nom> (<rang>)<ext> », ou rien si le fichier n'en est pas une.
        std::optional<std::uint64_t> parseRank(const std::string& fileName, const ExportName& name) {
            const std::string prefix = name.stem + " (";
            const std::string suffix = ")" + name.extension;
            if (fileName.size() <= prefix.size() + suffix.size() || fileName.compare(0, prefix.size(), prefix) != 0 ||
                fileName.compare(fileName.size() - suffix.size(), suffix.size(), suffix) != 0)
                return std::nullopt;
            const std::string digits = fileName.substr(prefix.size(), fileName.size() - prefix.size() - suffix.size());
            const bool isNumber = std::all_of(digits.begin(), digits.end(), [](char character) {
                return std::isdigit(static_cast<unsigned char>(character)) != 0;
            });
            if (!isNumber || digits.size() > MAXIMUM_RANK_DIGITS) return std::nullopt;
            return std::stoull(digits);
        }

        // Rang le plus élevé déjà présent dans le dossier, ou rien si aucun export de ce nom n'existe.
        std::optional<std::uint64_t> latestExportRank(const ExportName& name) {
            std::optional<std::uint64_t> latest;
            std::error_code errorCode;
            if (fs::exists(exportPath(name, 0), errorCode)) latest = 0;
            if (!fs::is_directory(name.directory, errorCode)) return latest;
            for (const fs::directory_entry& entry : fs::directory_iterator(name.directory)) {
                const auto rank = parseRank(IONs::pathToUtf8(entry.path().filename()), name);
                if (rank && (!latest || *rank > *latest)) latest = rank;
            }
            return latest;
        }

        // Fichier du prochain export : <nom><ext> s'il n'existe aucun export de ce nom, sinon le rang suivant.
        fs::path nextExportPath(const fs::path& directory, const std::string& filename) {
            const ExportName name = splitExportName(directory, filename);
            const auto latest = latestExportRank(name);
            return exportPath(name, latest ? *latest + 1 : 0);
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
        
        bool exportJpeg(const DataNs::Mosaic& mosaic, const fs::path& imagePath, ExportOutput& output, Json& result) {
            cv::Mat image;
            std::string errorMessage;
            if (!IONs::PyramidalTiffReaderClass(mosaic.imagePath).readRegion(
                    0, cv::Rect({0, 0}, mosaic.canvasSize), image, errorMessage)) {
                result["error"] = "cannot read mosaic image for JPEG export: " + errorMessage;
                return false;
            }
            cv::Mat image8;
            image.convertTo(image8, CV_8UC3, 1.0 / SIXTEEN_TO_EIGHT_BIT_DIVISOR);
            cv::Mat bgrImage;
            cv::cvtColor(image8, bgrImage, cv::COLOR_RGB2BGR);
            const fs::path temporaryPath = IONs::makeTemporaryPath(imagePath).concat(".jpg");
            if (!IONs::writeImage(temporaryPath, bgrImage)) {
                std::error_code removeError;
                fs::remove(temporaryPath, removeError);
                result["error"] = "cannot write JPEG mosaic '" + IONs::pathToUtf8(imagePath) + "'";
                return false;
            }
            if (!IONs::replaceFile(temporaryPath, imagePath, errorMessage)) {
                std::error_code removeError;
                fs::remove(temporaryPath, removeError);
                result["error"] = errorMessage;
                return false;
            }
            output = {imagePath, mosaic.canvasSize, false, "jpeg"};
            result["image"] = {{"path", IONs::pathToUtf8(imagePath)}, {"format", "jpeg"}};
            result["canvasSize"] = {output.canvasSize.width, output.canvasSize.height};
            return true;
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

            const std::string format = configuredFormat(config);
            if (format != "tiff" && format != "jpeg") {
                result["error"] = "unsupported export format '" + format + "'";
                return false;
            }
            const std::string filename = configuredFilename(config);
            const fs::path imagePath = nextExportPath(directory, filename);

            if (format == "jpeg") {
                return exportJpeg(mosaic, imagePath, output, result);
            }
            if (!IONs::copyFileReplacing(mosaic.imagePath, imagePath, errorMessage)) {
                result["error"] = errorMessage;
                return false;
            }
            output = {imagePath, mosaic.canvasSize, header.isBigTiff, format};
            result["image"] = {{"path", IONs::pathToUtf8(imagePath)}, {"bigTiff", output.isBigTiff},
                               {"format", format}};
            result["canvasSize"] = {output.canvasSize.width, output.canvasSize.height};
            return true;
        }

    }

    ExportStepClass::ExportStepClass(const ExportInput& initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "export";
        stepConfig.parameters = {{"string", "outputDirectory", initialInput.outputDirectory},
                                 {"string", "filename", initialInput.filename},
                                 {"string", "format", initialInput.format}};
        stepConfig.inputs = {{"Mosaic", "mosaic"}};
        configJson = stepConfig;
    }

    // Le checkpoint de l'export reste valable tant que le dernier fichier exporté (celui de rang maximal, voir
    // nextExportPath) est là, inchangé. Après execute(), c'est le fichier qui vient d'être écrit ; avant (reprise d'un
    // run), on le retrouve sur le disque d'après les paramètres. Les deux coïncident tant que personne ne touche au
    // dossier : la reprise ne refait pas une copie de plus. Fichier supprimé ou modifié : l'export est refait.
    Json ExportStepClass::externalInputsFingerprint() const {
        try {
            if (!output.imagePath.empty()) return IONs::computeFileFingerprint(output.imagePath);
            const ExportName name = splitExportName(configuredOutputDirectory(configJson), configuredFilename(configJson));
            const auto latest = latestExportRank(name);
            return latest ? IONs::computeFileFingerprint(exportPath(name, *latest)) : Json();
        } catch (const std::exception&) {
            return Json();   // dossier introuvable ou illisible : checkpoint périmé, execute() dira pourquoi
        }
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