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
#include <opencv2/imgproc.hpp>
#include <tiff.h>
#include <tiffio.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

// clang-format off
#include <jpeglib.h>
// clang-format on

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
        constexpr int JPEG_MAX_SIDE = 65500;
        constexpr int JPEG_BASELINE_QUALITY = 85;
        constexpr int RGB_CHANNELS = 3;
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

        std::string sanitizeFilename(const std::string& filename) {
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

        std::string configuredFilename(const Json& configJson) {
            if (!configJson.contains("parameters") || !configJson["parameters"].is_array()) {
                return "mosaic.tif";
            }
            for (const Json& parameter : configJson.at("parameters")) {
                if (parameter.value("name", "") == "filename" && parameter.contains("value") &&
                    parameter["value"].is_string()) {
                    return sanitizeFilename(parameter["value"].get<std::string>());
                }
            }
            return "mosaic.tif";
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

        bool hasJpegExtension(const fs::path& path) {
            std::string extension = IONs::pathToUtf8(path.extension());
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return extension == ".jpg" || extension == ".jpeg";
        }

        struct JpegErrorContext {
            jpeg_error_mgr pub{};
            std::jmp_buf jumpBuffer{};
            char message[JMSG_LENGTH_MAX]{};
        };

        void handleJpegErrorExit(j_common_ptr cinfo) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            auto* errorContext = reinterpret_cast<JpegErrorContext*>(cinfo->err);
            (*cinfo->err->format_message)(cinfo, errorContext->message);
            std::longjmp(errorContext->jumpBuffer, 1);
        }

        std::FILE* openBinaryOutput(const fs::path& path) {
#ifdef _WIN32
            return _wfopen(path.c_str(), L"wb");
#else
            return std::fopen(path.c_str(), "wb");
#endif
        }

        struct JpegStreamPlan {
            cv::Rect region;
            cv::Size outputSize;
            double scale = 1.0;
        };

        bool buildJpegStreamPlan(const IONs::PyramidalTiffReaderClass& reader, const cv::Rect& requestedRegion,
                                 JpegStreamPlan& plan, std::string& errorMessage) {
            int fullWidth = 0;
            int fullHeight = 0;
            if (!reader.getLevelDimensions(0, fullWidth, fullHeight, errorMessage)) return false;

            const cv::Rect fullCanvas(0, 0, fullWidth, fullHeight);
            plan.region = requestedRegion.empty() ? fullCanvas : (requestedRegion & fullCanvas);
            if (plan.region.empty()) {
                errorMessage = "the export region is empty";
                return false;
            }
            const int maxSide = std::max(plan.region.width, plan.region.height);
            plan.scale = maxSide > JPEG_MAX_SIDE ? static_cast<double>(JPEG_MAX_SIDE) / maxSide : 1.0;
            plan.outputSize = {std::max(1, static_cast<int>(std::round(plan.region.width * plan.scale))),
                               std::max(1, static_cast<int>(std::round(plan.region.height * plan.scale)))};
            return true;
        }

        bool convertBlockToRgb8(const cv::Mat& block, cv::Mat& output, std::string& errorMessage) {
            if (block.channels() != RGB_CHANNELS) {
                errorMessage = "TIFF image must have 3 RGB channels for JPEG export";
                return false;
            }
            if (block.depth() == CV_16U) {
                block.convertTo(output, CV_8UC3, 1.0 / SIXTEEN_TO_EIGHT_BIT_DIVISOR);
                return true;
            }
            if (block.depth() == CV_8U) {
                output = block;
                return true;
            }
            errorMessage = "TIFF image depth must be 8-bit or 16-bit for JPEG export";
            return false;
        }

        struct StripTarget {
            int sourceY = 0;
            int sourceHeight = 0;
            cv::Mat buffer;
        };

        bool fillStripBuffer(const IONs::PyramidalTiffReaderClass& reader, const JpegStreamPlan& plan,
                             StripTarget& strip, std::string& errorMessage) {
            for (int offsetX = 0; offsetX < plan.region.width; offsetX += DataNs::BLOCK_SIZE) {
                const int blockWidth = std::min(DataNs::BLOCK_SIZE, plan.region.width - offsetX);
                const cv::Rect sourceRect(plan.region.x + offsetX, plan.region.y + strip.sourceY, blockWidth,
                                          strip.sourceHeight);
                cv::Mat blockRaw;
                cv::Mat block8;
                if (!reader.readRegion(0, sourceRect, blockRaw, errorMessage) ||
                    !convertBlockToRgb8(blockRaw, block8, errorMessage)) {
                    return false;
                }
                const int destX0 = static_cast<int>(std::round(offsetX * plan.scale));
                const int destX1 = (offsetX + blockWidth == plan.region.width)
                                       ? plan.outputSize.width
                                       : static_cast<int>(std::round((offsetX + blockWidth) * plan.scale));
                if (destX1 <= destX0) continue;
                const cv::Rect destRect(destX0, 0, destX1 - destX0, strip.buffer.rows);
                if (destRect.size() == block8.size()) {
                    block8.copyTo(strip.buffer(destRect));
                } else {
                    cv::resize(block8, strip.buffer(destRect), destRect.size(), 0.0, 0.0, cv::INTER_AREA);
                }
            }
            return true;
        }

        void writeStripScanlines(jpeg_compress_struct& cinfo, const cv::Mat& stripRgb8) {
            for (int row = 0; row < stripRgb8.rows; ++row) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
                JSAMPROW rowPointer = const_cast<JSAMPROW>(stripRgb8.ptr<JSAMPLE>(row));
                jpeg_write_scanlines(&cinfo, &rowPointer, 1);
            }
        }

        bool encodeJpegStrips(const IONs::PyramidalTiffReaderClass& reader, const JpegStreamPlan& plan,
                              jpeg_compress_struct& cinfo, std::string& errorMessage) {
            for (int offsetY = 0; offsetY < plan.region.height; offsetY += DataNs::BLOCK_SIZE) {
                const int sourceHeight = std::min(DataNs::BLOCK_SIZE, plan.region.height - offsetY);
                const int destY0 = static_cast<int>(std::round(offsetY * plan.scale));
                const int destY1 = (offsetY + sourceHeight == plan.region.height)
                                       ? plan.outputSize.height
                                       : static_cast<int>(std::round((offsetY + sourceHeight) * plan.scale));
                const int destHeight = destY1 - destY0;
                if (destHeight <= 0) continue;

                StripTarget strip{offsetY, sourceHeight, cv::Mat(destHeight, plan.outputSize.width, CV_8UC3)};
                if (!fillStripBuffer(reader, plan, strip, errorMessage)) return false;
                writeStripScanlines(cinfo, strip.buffer);
            }
            return true;
        }

        void configureBaselineJpeg(jpeg_compress_struct& cinfo, std::FILE* outputFile, const cv::Size& outputSize) {
            jpeg_stdio_dest(&cinfo, outputFile);
            cinfo.image_width = static_cast<JDIMENSION>(outputSize.width);
            cinfo.image_height = static_cast<JDIMENSION>(outputSize.height);
            cinfo.input_components = RGB_CHANNELS;
            cinfo.in_color_space = JCS_RGB;
            jpeg_set_defaults(&cinfo);
            jpeg_set_quality(&cinfo, JPEG_BASELINE_QUALITY, TRUE);
        }

        bool streamTiffToJpegFile(const IONs::PyramidalTiffReaderClass& reader, const JpegStreamPlan& plan,
                                  const fs::path& temporaryPath, std::string& errorMessage) {
            std::FILE* outputFile = openBinaryOutput(temporaryPath);
            if (!outputFile) {
                errorMessage = "cannot open temporary JPEG file '" + IONs::pathToUtf8(temporaryPath) + "'";
                return false;
            }
            jpeg_compress_struct cinfo{};
            JpegErrorContext errorContext;
            cinfo.err = jpeg_std_error(&errorContext.pub);
            errorContext.pub.error_exit = &handleJpegErrorExit;
            if (setjmp(errorContext.jumpBuffer) != 0) {
                jpeg_destroy_compress(&cinfo);
                std::fclose(outputFile);
                errorMessage = std::string("JPEG encoding failed: ") + errorContext.message;
                return false;
            }
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
            jpeg_create_compress(&cinfo);
            configureBaselineJpeg(cinfo, outputFile, plan.outputSize);
            jpeg_start_compress(&cinfo, TRUE);
            const bool encoded = encodeJpegStrips(reader, plan, cinfo, errorMessage);
            if (encoded) jpeg_finish_compress(&cinfo);
            jpeg_destroy_compress(&cinfo);
            if (std::fclose(outputFile) != 0 && encoded) {
                errorMessage = "cannot close temporary JPEG file '" + IONs::pathToUtf8(temporaryPath) + "'";
                return false;
            }
            return encoded;
        }

        struct JpegExportRequest {
            fs::path tiffPath;
            cv::Rect region;
            fs::path outputPath;
        };

        bool exportTiffRegionToBaselineJpegImpl(const JpegExportRequest& request, cv::Size& exportedSize,
                                                std::string& errorMessage) {
            const IONs::PyramidalTiffReaderClass reader(request.tiffPath);
            JpegStreamPlan plan;
            if (!buildJpegStreamPlan(reader, request.region, plan, errorMessage)) return false;

            const fs::path parentDirectory = request.outputPath.parent_path();
            if (!parentDirectory.empty()) {
                std::error_code directoryError;
                fs::create_directories(parentDirectory, directoryError);
                if (directoryError) {
                    errorMessage = "cannot create output directory '" + IONs::pathToUtf8(parentDirectory) +
                                   "': " + directoryError.message();
                    return false;
                }
            }

            const fs::path temporaryPath = IONs::makeTemporaryPath(request.outputPath);
            if (!streamTiffToJpegFile(reader, plan, temporaryPath, errorMessage) ||
                !IONs::replaceFile(temporaryPath, request.outputPath, errorMessage)) {
                std::error_code removeError;
                fs::remove(temporaryPath, removeError);
                return false;
            }
            exportedSize = plan.outputSize;
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
            const std::string filename = configuredFilename(config);
            const fs::path imagePath = nextExportPath(directory, filename);
            if (hasJpegExtension(imagePath)) {
                cv::Size exportedSize;
                const JpegExportRequest request{mosaic.imagePath, cv::Rect(), imagePath};
                if (!exportTiffRegionToBaselineJpegImpl(request, exportedSize, errorMessage)) {
                    result["error"] = errorMessage;
                    return false;
                }
                output = {imagePath, exportedSize, false};
            } else {
                if (!IONs::copyFileReplacing(mosaic.imagePath, imagePath, errorMessage)) {
                    result["error"] = errorMessage;
                    return false;
                }
                output = {imagePath, mosaic.canvasSize, header.isBigTiff};
            }
            result["image"] = {{"path", IONs::pathToUtf8(imagePath)}, {"bigTiff", output.isBigTiff}};
            result["canvasSize"] = {output.canvasSize.width, output.canvasSize.height};
            return true;
        }

    }

    bool exportTiffRegionToBaselineJpeg(const std::filesystem::path& tiffPath,
                                        const cv::Rect& region,
                                        const std::filesystem::path& outputPath,
                                        std::string& errorMessage) {
        cv::Size exportedSize;
        const JpegExportRequest request{tiffPath, region, outputPath};
        return exportTiffRegionToBaselineJpegImpl(request, exportedSize, errorMessage);
    }

    ExportStepClass::ExportStepClass(const ExportInput& initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "export";
        stepConfig.parameters = {{"string", "outputDirectory", initialInput.outputDirectory},
                                 {"string", "filename", initialInput.filename}};
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
