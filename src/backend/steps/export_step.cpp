#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "steps/export_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/tile.h"

#include <tiffio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace StepsNs {

    namespace fs = std::filesystem;

    namespace {

        using BlockIndex = std::map<std::pair<int, int>, const DataNs::CompositeBlock*>;

        struct OutputPaths {
            fs::path image;
        };

        struct PendingFile {
            fs::path temporaryPath;
            fs::path destinationPath;
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
            return fs::current_path() / "output";
        }

        OutputPaths makeOutputPaths(const fs::path& directory) {
            return {directory / "mosaic.tif"};
        }

        fs::path temporaryPath(const fs::path& path) {
            static std::atomic<std::uint64_t> sequence{0};
            const auto timeValue = std::chrono::steady_clock::now().time_since_epoch().count();
            fs::path result = path;
            result += "." + std::to_string(timeValue) + "." + std::to_string(sequence.fetch_add(1)) + ".tmp";
            return result;
        }

        bool validateBlock(const DataNs::CompositeBlock& block, std::string& errorMessage) {
            const cv::Rect& rectangle = block.blockRect;
            if (rectangle.x < 0 || rectangle.y < 0 || rectangle.width <= 0 || rectangle.height <= 0 ||
                rectangle.width > DataNs::BLOCK_SIZE || rectangle.height > DataNs::BLOCK_SIZE ||
                rectangle.x % DataNs::BLOCK_SIZE != 0 || rectangle.y % DataNs::BLOCK_SIZE != 0) {
                errorMessage = "composite block rectangle must be positive and aligned to the block grid";
                return false;
            }
            if (block.imageRgb.dims != 2 || block.imageRgb.type() != CV_16UC3 ||
                block.imageRgb.size() != rectangle.size()) {
                errorMessage = "composite block image has an unexpected type or size";
                return false;
            }
            return true;
        }

        bool addBlockToIndex(const DataNs::CompositeBlock& block, BlockIndex& blockByGridPosition,
                             int& canvasWidth, int& canvasHeight, std::string& errorMessage) {
            if (!validateBlock(block, errorMessage)) return false;
            const cv::Rect& rectangle = block.blockRect;
            const std::int64_t right = static_cast<std::int64_t>(rectangle.x) + rectangle.width;
            const std::int64_t bottom = static_cast<std::int64_t>(rectangle.y) + rectangle.height;
            if (right > std::numeric_limits<int>::max() || bottom > std::numeric_limits<int>::max()) {
                errorMessage = "composite canvas dimensions exceed the supported range";
                return false;
            }
            const auto gridPosition = std::make_pair(rectangle.x / DataNs::BLOCK_SIZE, rectangle.y / DataNs::BLOCK_SIZE);
            if (!blockByGridPosition.emplace(gridPosition, &block).second) {
                errorMessage = "multiple composite blocks occupy the same grid position";
                return false;
            }
            canvasWidth = std::max(canvasWidth, static_cast<int>(right));
            canvasHeight = std::max(canvasHeight, static_cast<int>(bottom));
            return true;
        }

        bool validateBlockGrid(const BlockIndex& blockByGridPosition, cv::Size canvasSize, std::string& errorMessage) {
            const int columnCount = 1 + (canvasSize.width - 1) / DataNs::BLOCK_SIZE;
            const int rowCount = 1 + (canvasSize.height - 1) / DataNs::BLOCK_SIZE;
            const std::size_t expectedBlockCount = static_cast<std::size_t>(columnCount) * static_cast<std::size_t>(rowCount);
            if (blockByGridPosition.size() != expectedBlockCount) {
                errorMessage = "composite blocks do not cover the complete output grid";
                return false;
            }
            for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
                for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
                    const auto blockEntry = blockByGridPosition.find({columnIndex, rowIndex});
                    if (blockEntry == blockByGridPosition.end()) {
                        errorMessage = "composite blocks do not cover the complete output grid";
                        return false;
                    }
                    const cv::Rect& rectangle = blockEntry->second->blockRect;
                    if (rectangle.width != std::min(DataNs::BLOCK_SIZE, canvasSize.width - rectangle.x) ||
                        rectangle.height != std::min(DataNs::BLOCK_SIZE, canvasSize.height - rectangle.y)) {
                        errorMessage = "composite block dimensions do not match its position in the output grid";
                        return false;
                    }
                }
            }
            return true;
        }

        bool indexBlocks(const DataNs::CompositeBlockList& blocks, BlockIndex& blockByGridPosition,
                         cv::Size& canvasSize, std::string& errorMessage) {
            if (blocks.empty()) {
                errorMessage = "no composite blocks were provided";
                return false;
            }
            int canvasWidth = 0;
            int canvasHeight = 0;
            for (const DataNs::CompositeBlock& block : blocks)
                if (!addBlockToIndex(block, blockByGridPosition, canvasWidth, canvasHeight, errorMessage)) return false;
            canvasSize = {canvasWidth, canvasHeight};
            return validateBlockGrid(blockByGridPosition, canvasSize, errorMessage);
        }

        bool configureTiff(TIFF* tiffFile, cv::Size canvasSize, const cv::Mat& sampleImage, std::string& errorMessage) {
            const std::uint16_t channelCount = static_cast<std::uint16_t>(sampleImage.channels());
            const std::uint16_t bitsPerSample = static_cast<std::uint16_t>(sampleImage.elemSize1() * 8);
            const std::uint16_t photometric = channelCount == 1 ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB;
            if (!TIFFSetField(tiffFile, TIFFTAG_IMAGEWIDTH, static_cast<std::uint32_t>(canvasSize.width)) ||
                !TIFFSetField(tiffFile, TIFFTAG_IMAGELENGTH, static_cast<std::uint32_t>(canvasSize.height)) ||
                !TIFFSetField(tiffFile, TIFFTAG_SAMPLESPERPIXEL, channelCount) ||
                !TIFFSetField(tiffFile, TIFFTAG_BITSPERSAMPLE, bitsPerSample) ||
                !TIFFSetField(tiffFile, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT) ||
                !TIFFSetField(tiffFile, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) ||
                !TIFFSetField(tiffFile, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
                !TIFFSetField(tiffFile, TIFFTAG_PHOTOMETRIC, photometric) ||
                !TIFFSetField(tiffFile, TIFFTAG_COMPRESSION, COMPRESSION_LZW) ||
                !TIFFSetField(tiffFile, TIFFTAG_TILEWIDTH, static_cast<std::uint32_t>(DataNs::BLOCK_SIZE)) ||
                !TIFFSetField(tiffFile, TIFFTAG_TILELENGTH, static_cast<std::uint32_t>(DataNs::BLOCK_SIZE))) {
                errorMessage = "cannot configure TIFF image";
                return false;
            }
            return true;
        }

        bool shouldUseBigTiff(cv::Size canvasSize, const cv::Mat& sampleImage) {
            const long double pixelCount = static_cast<long double>(canvasSize.width) * canvasSize.height;
            const long double bytesPerPixel = sampleImage.elemSize();
            const long double tileCount =
                static_cast<long double>(1 + (canvasSize.width - 1) / DataNs::BLOCK_SIZE) *
                (1 + (canvasSize.height - 1) / DataNs::BLOCK_SIZE);
            const long double conservativeFileSize = pixelCount * bytesPerPixel * 1.5L + tileCount * 256 + 1024 * 1024;
            return conservativeFileSize >= std::numeric_limits<std::uint32_t>::max();
        }

        bool writeTileGrid(TIFF* tiffFile, const BlockIndex& blockByGridPosition, cv::Size canvasSize,
                           std::string& errorMessage) {
            const cv::Mat& sampleImage = blockByGridPosition.begin()->second->imageRgb;
            const std::size_t tileBytes = static_cast<std::size_t>(DataNs::BLOCK_SIZE) * DataNs::BLOCK_SIZE * sampleImage.elemSize();
            cv::Mat tileBuffer = cv::Mat::zeros(DataNs::BLOCK_SIZE, DataNs::BLOCK_SIZE, sampleImage.type());
            const int columnCount = 1 + (canvasSize.width - 1) / DataNs::BLOCK_SIZE;
            const int rowCount = 1 + (canvasSize.height - 1) / DataNs::BLOCK_SIZE;
            for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
                for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
                    const DataNs::CompositeBlock& block = *blockByGridPosition.at({columnIndex, rowIndex});
                    const cv::Mat& blockImage = block.imageRgb;
                    tileBuffer.setTo(cv::Scalar::all(0));
                    blockImage.copyTo(tileBuffer(cv::Rect{0, 0, blockImage.cols, blockImage.rows}));
                    const ttile_t tileIndex = TIFFComputeTile(
                        tiffFile,
                        static_cast<std::uint32_t>(block.blockRect.x),
                        static_cast<std::uint32_t>(block.blockRect.y),
                        0,
                        0
                    );
                    if (TIFFWriteEncodedTile(tiffFile, tileIndex, tileBuffer.data, static_cast<tmsize_t>(tileBytes)) == -1) {
                        errorMessage = "cannot write TIFF tile";
                        return false;
                    }
                }
            }
            return true;
        }

        bool writeImageTiff(const BlockIndex& blockByGridPosition, cv::Size canvasSize,
                            const fs::path& outputPath, bool& isBigTiff, std::string& errorMessage) {
            const cv::Mat& sampleImage = blockByGridPosition.begin()->second->imageRgb;
            isBigTiff = shouldUseBigTiff(canvasSize, sampleImage);
            TIFF* rawTiffFile = TIFFOpen(outputPath.string().c_str(), isBigTiff ? "w8" : "w");
            if (!rawTiffFile) {
                errorMessage = "cannot open output TIFF '" + outputPath.string() + "'";
                return false;
            }
            const std::unique_ptr<TIFF, decltype(&TIFFClose)> tiffFile(rawTiffFile, &TIFFClose);

            if (!configureTiff(tiffFile.get(), canvasSize, sampleImage, errorMessage)) return false;
            if (!writeTileGrid(tiffFile.get(), blockByGridPosition, canvasSize, errorMessage)) return false;
            if (!TIFFWriteDirectory(tiffFile.get())) {
                errorMessage = "cannot finalize TIFF directory";
                return false;
            }
            return true;
        }

        void publishFile(const PendingFile& file) {
#ifdef _WIN32
            if (!MoveFileExW(file.temporaryPath.c_str(), file.destinationPath.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "cannot replace output '" + file.destinationPath.string() + "'");
            }
#else
            std::error_code errorCode;
            fs::rename(file.temporaryPath, file.destinationPath, errorCode);
            if (errorCode) throw fs::filesystem_error("cannot replace output", file.temporaryPath, file.destinationPath, errorCode);
#endif
        }

        struct TemporaryFileCleanup {
            std::vector<fs::path> paths;
            ~TemporaryFileCleanup() {
                for (const fs::path& path : paths) {
                    std::error_code ignored;
                    fs::remove(path, ignored);
                }
            }
        };

        Json outputFingerprint(const OutputPaths& paths) {
            const fs::path& path = paths.image;
            std::error_code errorCode;
            const bool exists = fs::exists(path, errorCode);
            if (errorCode) throw fs::filesystem_error("cannot inspect export output", path, errorCode);
            if (!exists) {
                return {{"path", path.string()}, {"exists", false}};
            }
            const std::uintmax_t fileSize = fs::file_size(path, errorCode);
            if (errorCode) throw fs::filesystem_error("cannot read export output size", path, errorCode);
            const auto modifiedTime = fs::last_write_time(path, errorCode);
            if (errorCode) throw fs::filesystem_error("cannot read export output time", path, errorCode);
            return {{"path", path.string()},
                    {"exists", true},
                    {"size", fileSize},
                    {"modified", modifiedTime.time_since_epoch().count()}};
        }

        fs::path configuredOutputDirectory(const Json& configJson) {
            std::string directory;
            if (configJson.contains("parameters") && configJson["parameters"].is_array()) {
                for (const Json& parameter : configJson.at("parameters")) {
                    if (parameter.value("name", "") == "outputDirectory") {
                        directory = parameter.value("value", "");
                        break;
                    }
                }
            }
            return directory.empty() ? downloadsDirectory() : fs::path(directory);
        }

    } // namespace

    ExportStepClass::ExportStepClass(const ExportInput& initialInput)
        : input(initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "export";
        stepConfig.parameters = {{"string", "outputDirectory", initialInput.outputDirectory}};
        stepConfig.inputs = {{"CompositeBlocks", "compositeBlocks"}};
        stepConfig.outputs = {};
        configJson = stepConfig;
    }

    Json ExportStepClass::externalInputsFingerprint() const {
        return outputFingerprint(makeOutputPaths(configuredOutputDirectory(configJson)));
    }

    bool ExportStepClass::execute() {
        result = Json::object();
        output = {};

        try {
            if (!dataStore) throw std::runtime_error("data store is not available");
            if (!dataStore->has("compositeBlocks")) {
                throw std::runtime_error("missing required input 'compositeBlocks'");
            }
            const DataNs::CompositeBlockList& blocks = dataStore->get<DataNs::CompositeBlockList>("compositeBlocks");

            BlockIndex blockByGridPosition;
            std::string errorMessage;
            if (!indexBlocks(blocks, blockByGridPosition, output.canvasSize, errorMessage)) {
                result["error"] = errorMessage;
                return false;
            }

            const fs::path directory = configuredOutputDirectory(configJson);
            std::error_code directoryError;
            fs::create_directories(directory, directoryError);
            if (directoryError) {
                result["error"] = "cannot create output directory '" + directory.string() + "': " + directoryError.message();
                return false;
            }

            const OutputPaths paths = makeOutputPaths(directory);
            const PendingFile pendingFile{temporaryPath(paths.image), paths.image};
            TemporaryFileCleanup cleanup;
            cleanup.paths.push_back(pendingFile.temporaryPath);
            fs::remove(pendingFile.temporaryPath);

            if (!writeImageTiff(blockByGridPosition, output.canvasSize,
                                pendingFile.temporaryPath, output.isBigTiff, errorMessage)) {
                result["error"] = errorMessage;
                return false;
            }
            publishFile(pendingFile);

            output.imagePath = paths.image;
            output.blockCount = blocks.size();
            result["image"] = {{"path", paths.image.string()}, {"bigTiff", output.isBigTiff}};
            result["canvasSize"] = {output.canvasSize.width, output.canvasSize.height};
            result["blockCount"] = output.blockCount;
            return true;
        } catch (const std::exception& exception) {
            result["error"] = exception.what();
            return false;
        }
    }

} // namespace StepsNs
