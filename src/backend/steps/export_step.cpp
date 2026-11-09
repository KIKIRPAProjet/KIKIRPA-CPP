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

#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <tiff.h>
#include <tiffio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
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

        struct BlockGridBuilder {
            BlockIndex blocks;
            int width = 0;
            int height = 0;
        };

        struct BlockGridShape {
            cv::Size canvasSize;
            int columnCount = 0;
            int rowCount = 0;
        };

        struct TiffWriteStatus {
            bool isBigTiff = false;
            std::string errorMessage;
        };

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
            throw std::runtime_error("cannot resolve the current user's Downloads directory");
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

        bool addBlockToIndex(const DataNs::CompositeBlock& block, BlockGridBuilder& grid, std::string& errorMessage) {
            if (!validateBlock(block, errorMessage)) return false;
            const cv::Rect& rectangle = block.blockRect;
            const std::int64_t right = static_cast<std::int64_t>(rectangle.x) + rectangle.width;
            const std::int64_t bottom = static_cast<std::int64_t>(rectangle.y) + rectangle.height;
            if (right > std::numeric_limits<int>::max() || bottom > std::numeric_limits<int>::max()) {
                errorMessage = "composite canvas dimensions exceed the supported range";
                return false;
            }
            const auto gridPosition = std::make_pair(rectangle.x / DataNs::BLOCK_SIZE, rectangle.y / DataNs::BLOCK_SIZE);
            if (!grid.blocks.emplace(gridPosition, &block).second) {
                errorMessage = "multiple composite blocks occupy the same grid position";
                return false;
            }
            grid.width = std::max(grid.width, static_cast<int>(right));
            grid.height = std::max(grid.height, static_cast<int>(bottom));
            return true;
        }

        bool validateBlockDimensions(const DataNs::CompositeBlock& block, cv::Size canvasSize,
                                     std::string& errorMessage) {
            const cv::Rect& rectangle = block.blockRect;
            if (rectangle.width != std::min(DataNs::BLOCK_SIZE, canvasSize.width - rectangle.x) ||
                rectangle.height != std::min(DataNs::BLOCK_SIZE, canvasSize.height - rectangle.y)) {
                errorMessage = "composite block dimensions do not match its position in the output grid";
                return false;
            }
            return true;
        }

        bool validateBlockRow(const BlockIndex& blocks, int rowIndex, const BlockGridShape& shape,
                              std::string& errorMessage) {
            for (int columnIndex = 0; columnIndex < shape.columnCount; ++columnIndex) {
                const auto blockEntry = blocks.find({columnIndex, rowIndex});
                if (blockEntry == blocks.end()) {
                    errorMessage = "composite blocks do not cover the complete output grid";
                    return false;
                }
                if (!validateBlockDimensions(*blockEntry->second, shape.canvasSize, errorMessage)) return false;
            }
            return true;
        }

        bool validateBlockGrid(const BlockIndex& blocks, cv::Size canvasSize, std::string& errorMessage) {
            const BlockGridShape shape{canvasSize,
                                       1 + (canvasSize.width - 1) / DataNs::BLOCK_SIZE,
                                       1 + (canvasSize.height - 1) / DataNs::BLOCK_SIZE};
            const std::size_t expectedBlockCount =
                static_cast<std::size_t>(shape.columnCount) * static_cast<std::size_t>(shape.rowCount);
            if (blocks.size() != expectedBlockCount) {
                errorMessage = "composite blocks do not cover the complete output grid";
                return false;
            }
            for (int rowIndex = 0; rowIndex < shape.rowCount; ++rowIndex)
                if (!validateBlockRow(blocks, rowIndex, shape, errorMessage)) return false;
            return true;
        }

        bool indexBlocks(const DataNs::CompositeBlockList& blocks, BlockIndex& blockByGridPosition,
                         cv::Size& canvasSize, std::string& errorMessage) {
            if (blocks.empty()) {
                errorMessage = "no composite blocks were provided";
                return false;
            }
            BlockGridBuilder grid;
            for (const DataNs::CompositeBlock& block : blocks)
                if (!addBlockToIndex(block, grid, errorMessage)) return false;
            canvasSize = {grid.width, grid.height};
            blockByGridPosition = std::move(grid.blocks);
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
            const std::size_t columnCount = static_cast<std::size_t>(1 + (canvasSize.width - 1) / DataNs::BLOCK_SIZE);
            const std::size_t rowCount = static_cast<std::size_t>(1 + (canvasSize.height - 1) / DataNs::BLOCK_SIZE);
            const long double tileCount = static_cast<long double>(columnCount) * rowCount;
            const long double conservativeFileSize = pixelCount * bytesPerPixel * 1.5L + tileCount * 256 + 1024 * 1024;
            return conservativeFileSize >= std::numeric_limits<std::uint32_t>::max();
        }

        struct TiffTileWriter {
            TIFF* file;
            cv::Size canvasSize;
            std::size_t tileBytes;
            cv::Mat buffer;

            bool write(const DataNs::CompositeBlock& block, std::string& errorMessage) {
                const cv::Mat& image = block.imageRgb;
                buffer.setTo(cv::Scalar::all(0));
                image.copyTo(buffer(cv::Rect{0, 0, image.cols, image.rows}));
                const ttile_t tileIndex =
                    TIFFComputeTile(file, static_cast<std::uint32_t>(block.blockRect.x),
                                    static_cast<std::uint32_t>(block.blockRect.y), 0, 0);
                if (TIFFWriteEncodedTile(file, tileIndex, buffer.data, static_cast<tmsize_t>(tileBytes)) == -1) {
                    errorMessage = "cannot write TIFF tile";
                    return false;
                }
                return true;
            }

            bool writeGrid(const BlockIndex& blocks, std::string& errorMessage) {
                const int columnCount = 1 + (canvasSize.width - 1) / DataNs::BLOCK_SIZE;
                const int rowCount = 1 + (canvasSize.height - 1) / DataNs::BLOCK_SIZE;
                for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex)
                    for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex)
                        if (!write(*blocks.at({columnIndex, rowIndex}), errorMessage)) return false;
                return true;
            }
        };

        bool writeImageTiff(const BlockIndex& blockByGridPosition, cv::Size canvasSize,
                            const fs::path& outputPath, TiffWriteStatus& status) {
            const cv::Mat& sampleImage = blockByGridPosition.begin()->second->imageRgb;
            status.isBigTiff = shouldUseBigTiff(canvasSize, sampleImage);
            TIFF* rawTiffFile = TIFFOpen(outputPath.string().c_str(), status.isBigTiff ? "w8" : "w");
            if (!rawTiffFile) {
                status.errorMessage = "cannot open output TIFF '" + outputPath.string() + "'";
                return false;
            }
            const std::unique_ptr<TIFF, decltype(&TIFFClose)> tiffFile(rawTiffFile, &TIFFClose);

            if (!configureTiff(tiffFile.get(), canvasSize, sampleImage, status.errorMessage)) return false;
            const std::size_t tileBytes = static_cast<std::size_t>(DataNs::BLOCK_SIZE) * DataNs::BLOCK_SIZE *
                                          sampleImage.elemSize();
            TiffTileWriter writer{tiffFile.get(), canvasSize, tileBytes,
                                  cv::Mat::zeros(DataNs::BLOCK_SIZE, DataNs::BLOCK_SIZE, sampleImage.type())};
            if (!writer.writeGrid(blockByGridPosition, status.errorMessage)) return false;
            if (!TIFFWriteDirectory(tiffFile.get())) {
                status.errorMessage = "cannot finalize TIFF directory";
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

        bool writeOutputFile(const BlockIndex& blocks, cv::Size canvasSize, const fs::path& destination,
                             TiffWriteStatus& status) {
            const PendingFile pendingFile{temporaryPath(destination), destination};
            TemporaryFileCleanup cleanup;
            cleanup.paths.push_back(pendingFile.temporaryPath);
            if (!writeImageTiff(blocks, canvasSize, pendingFile.temporaryPath, status)) return false;
            publishFile(pendingFile);
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

        void recordExportSuccess(const OutputPaths& paths, const DataNs::CompositeBlockList& blocks,
                                 ExportOutput& output, Json& result) {
            output.imagePath = paths.image;
            result["image"] = {{"path", paths.image.u8string()}, {"bigTiff", output.isBigTiff}};
            result["canvasSize"] = {output.canvasSize.width, output.canvasSize.height};
            output.blockCount = blocks.size();
            result["blockCount"] = output.blockCount;
        }

        bool exportBlocks(const DataNs::CompositeBlockList& blocks, const Json& config, ExportOutput& output,
                          Json& result) {
            BlockIndex blockByGridPosition;
            std::string errorMessage;
            if (!indexBlocks(blocks, blockByGridPosition, output.canvasSize, errorMessage)) {
                result["error"] = errorMessage;
                return false;
            }
            fs::path directory;
            if (!prepareOutputDirectory(config, directory, result)) return false;
            const OutputPaths paths = makeOutputPaths(directory);
            TiffWriteStatus writeStatus;
            if (!writeOutputFile(blockByGridPosition, output.canvasSize, paths.image, writeStatus)) {
                result["error"] = writeStatus.errorMessage;
                return false;
            }
            output.isBigTiff = writeStatus.isBigTiff;
            recordExportSuccess(paths, blocks, output, result);
            return true;
        }

    }

    ExportStepClass::ExportStepClass(const ExportInput& initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "export";
        stepConfig.parameters = {{"string", "outputDirectory", initialInput.outputDirectory}};
        stepConfig.inputs = {{"CompositeBlocks", "compositeBlocks"}};
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
            const DataNs::CompositeBlockList& blocks = dataStore->get<DataNs::CompositeBlockList>("compositeBlocks");
            if (!exportBlocks(blocks, configJson, output, result)) return false;
            return true;
        } catch (const std::exception& exception) {
            result["error"] = exception.what();
            return false;
        }
    }

}
