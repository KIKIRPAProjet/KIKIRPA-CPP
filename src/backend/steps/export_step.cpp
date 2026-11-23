// src/backend/steps/export_step.cpp
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "steps/export_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/io/file_fingerprint.h"
#include "core/io/file_replacement.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/unicode_path.h"

#include <nlohmann/json.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <tiff.h>
#include <tiffio.h>

#include <algorithm>
#include <cctype>
#include <cmath>
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
        
        constexpr int DEFAULT_PREVIEW_MAX_SIDE = 6000;
        constexpr int JPEG_MAX_SIDE = 65535;

        int configuredPreviewMaxSide(const Json& configJson) {
            if (!configJson.contains("parameters") || !configJson["parameters"].is_array()) {
                return DEFAULT_PREVIEW_MAX_SIDE;
            }
            for (const Json& parameter : configJson.at("parameters")) {
                if (parameter.value("name", "") == "previewMaxSide" && parameter.contains("value") &&
                    parameter["value"].is_number_integer()) {
                    return parameter["value"].get<int>();
                }
            }
            return DEFAULT_PREVIEW_MAX_SIDE;
        }

        bool exportJpegPreview(const DataNs::Mosaic& mosaic, const fs::path& imagePath,
                               const Json& config, ExportOutput& output, Json& result) {
            const int maxSide = configuredPreviewMaxSide(config);
            if (maxSide < 1 || maxSide > JPEG_MAX_SIDE) {
                result["error"] = "JPEG preview size must be between 1 and 65535 pixels";
                return false;
            }

            const int sourceSide = std::max(mosaic.canvasSize.width, mosaic.canvasSize.height);
            const double scale = std::min(static_cast<double>(maxSide) / sourceSide, 1.0);
            const cv::Size previewSize{
                std::max(1, static_cast<int>(std::round(mosaic.canvasSize.width * scale))),
                std::max(1, static_cast<int>(std::round(mosaic.canvasSize.height * scale)))
            };

            cv::Mat preview(previewSize, CV_8UC3, cv::Scalar(0, 0, 0));
            IONs::PyramidalTiffReaderClass reader(mosaic.imagePath);

            for (int sourceY = 0; sourceY < mosaic.canvasSize.height; sourceY += DataNs::BLOCK_SIZE) {
                for (int sourceX = 0; sourceX < mosaic.canvasSize.width; sourceX += DataNs::BLOCK_SIZE) {
                    const cv::Rect sourceRect(
                        sourceX, sourceY,
                        std::min(DataNs::BLOCK_SIZE, mosaic.canvasSize.width - sourceX),
                        std::min(DataNs::BLOCK_SIZE, mosaic.canvasSize.height - sourceY)
                    );

                    cv::Mat source16;
                    std::string errorMessage;
                    if (!reader.readRegion(0, sourceRect, source16, errorMessage)) {
                        result["error"] = "cannot read mosaic region for JPEG preview: " + errorMessage;
                        return false;
                    }

                    cv::Mat source8;
                    source16.convertTo(source8, CV_8UC3, 1.0 / SIXTEEN_TO_EIGHT_BIT_DIVISOR);

                    cv::Mat sourceBgr;
                    cv::cvtColor(source8, sourceBgr, cv::COLOR_RGB2BGR);

                    const cv::Rect previewRect(
                        static_cast<int>(std::round(sourceX * scale)),
                        static_cast<int>(std::round(sourceY * scale)),
                        std::max(1, static_cast<int>(std::round((sourceX + sourceRect.width) * scale)) -
                            static_cast<int>(std::round(sourceX * scale))),
                        std::max(1, static_cast<int>(std::round((sourceY + sourceRect.height) * scale)) -
                            static_cast<int>(std::round(sourceY * scale)))
                    );
                    const cv::Rect clippedPreview = previewRect & cv::Rect(0, 0, previewSize.width, previewSize.height);
                    if (clippedPreview.empty()) continue;
                    cv::resize(sourceBgr, preview(clippedPreview), clippedPreview.size(), 0.0, 0.0, cv::INTER_AREA);
                }
            }

            const fs::path temporaryPath = IONs::makeTemporaryPath(imagePath).concat(".jpg");
            if (!IONs::writeImage(temporaryPath, preview)) {
                std::error_code removeError;
                fs::remove(temporaryPath, removeError);
                result["error"] = "cannot write JPEG preview '" + IONs::pathToUtf8(imagePath) + "'";
                return false;
            }

            std::string errorMessage;
            if (!IONs::replaceFile(temporaryPath, imagePath, errorMessage)) {
                std::error_code removeError;
                fs::remove(temporaryPath, removeError);
                result["error"] = errorMessage;
                return false;
            }

            output = {imagePath, previewSize, false, "jpeg"};
            result["image"] = {{"path", IONs::pathToUtf8(imagePath)}, {"format", "jpeg"},
                               {"preview", true}, {"previewMaxSide", previewSize.width > previewSize.height ? previewSize.width : previewSize.height}};
            result["canvasSize"] = {previewSize.width, previewSize.height};
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
            if (format != "tiff" && format != "jpeg" && format != "both") {
                result["error"] = "unsupported export format '" + format + "'";
                return false;
            }
            const std::string filename = configuredFilename(config);
            const fs::path imagePath = nextExportPath(directory, filename);

            if (format == "jpeg") {
                return exportJpegPreview(mosaic, imagePath, config, output, result);
            }
            if (format == "both") {
                if (!IONs::copyFileReplacing(mosaic.imagePath, imagePath, errorMessage)) {
                    result["error"] = errorMessage;
                    return false;
                }

                const fs::path jpegName = fs::path(IONs::pathToUtf8(imagePath)).stem().replace_extension(".jpg");
                const fs::path jpegPath = nextExportPath(directory, IONs::pathToUtf8(jpegName.filename()));
                Json jpegConfig = config;
                jpegConfig["parameters"] = Json::array();
                for (const Json& parameter : config.at("parameters")) {
                    if (parameter.value("name", "") == "format") {
                        jpegConfig["parameters"].push_back({{"valueType", "string"}, {"name", "format"}, {"value", "jpeg"}});
                    } else {
                        jpegConfig["parameters"].push_back(parameter);
                    }
                }

                ExportOutput jpegOutput;
                Json jpegResult;
                if (!exportJpegPreview(mosaic, jpegPath, jpegConfig, jpegOutput, jpegResult)) {
                    result = jpegResult;
                    return false;
                }

                output = {imagePath, mosaic.canvasSize, header.isBigTiff, "both"};
                result["image"] = {{"path", IONs::pathToUtf8(imagePath)}, {"bigTiff", output.isBigTiff},
                                   {"format", "tiff"}, {"jpegPreview", IONs::pathToUtf8(jpegOutput.imagePath)}};
                result["canvasSize"] = {output.canvasSize.width, output.canvasSize.height};
                return true;
            } else {
                if (!IONs::copyFileReplacing(mosaic.imagePath, imagePath, errorMessage)) {
                    result["error"] = errorMessage;
                    return false;
                }
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
                                 {"string", "format", initialInput.format},
                                 {"int", "previewMaxSide", initialInput.previewMaxSide}};
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