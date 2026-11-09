// src/backend/algorithms/cropping/mosaic_crop.cpp
#include "algorithms/cropping/mosaic_crop.h"

#include <opencv2/core/base.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace CroppingNs {

    namespace {

        constexpr std::size_t FILE_COUNT = 3;
        constexpr std::size_t PNG_HEADER_SIZE = 33;
        constexpr std::size_t PNG_WIDTH_OFFSET = 16;
        constexpr std::size_t PNG_HEIGHT_OFFSET = 20;
        constexpr std::size_t PNG_DEPTH_OFFSET = 24;
        constexpr std::size_t PNG_COLOR_TYPE_OFFSET = 25;
        constexpr std::size_t BYTE_BIT_COUNT = 8;
        constexpr unsigned char COLOR_DEPTH = 16;
        constexpr unsigned char MASK_DEPTH = 8;
        constexpr unsigned char RGB_COLOR_TYPE = 2;
        constexpr unsigned char GRAYSCALE_COLOR_TYPE = 0;

        constexpr std::array<unsigned char, 8> PNG_SIGNATURE{
            137, 80, 78, 71, 13, 10, 26, 10
        };

        constexpr std::array<unsigned char, 8> PNG_HEADER_MARKER{
            0, 0, 0, 13, 'I', 'H', 'D', 'R'
        };

        const std::array<std::string, FILE_COUNT> FILE_PREFIXES{
            "rgb", "provenance", "mask"
        };

        constexpr std::array<int, FILE_COUNT> IMAGE_TYPES{
            CV_16UC3, CV_16UC1, CV_8UC1
        };

        struct BlockFiles {

            cv::Rect rectangle;
            std::array<std::filesystem::path, FILE_COUNT> paths;

        };

        struct CropBlock {

            BlockFiles source;
            cv::Rect intersection;

        };

        struct CoverageEvent {

            int positionX = 0;
            bool isStart = false;
            cv::Rect rectangle;

        };

        bool fail(
            std::string& errorMessage,
            const std::string& message
        ) {
            errorMessage = message;
            return false;
        }

        bool hasSupportedCoordinates(const cv::Rect& rectangle) {
            if (rectangle.width <= 0 || rectangle.height <= 0) {
                return false;
            }

            const std::int64_t right =
                static_cast<std::int64_t>(rectangle.x) + rectangle.width;
            const std::int64_t bottom =
                static_cast<std::int64_t>(rectangle.y) + rectangle.height;
            const int maximum = std::numeric_limits<int>::max();

            return right <= maximum && bottom <= maximum;
        }

        bool validateInput(
            const CropInput& input,
            std::string& errorMessage
        ) {
            if (input.mosaic.mosaicPath.empty()) {
                return fail(errorMessage, "Input mosaic directory is empty");
            }
            if (input.mosaic.totalBlocks <= 0) {
                return fail(errorMessage,
                    "Input mosaic block count must be positive");
            }
            if (!hasSupportedCoordinates(input.rectangle)) {
                return fail(errorMessage,
                    "Crop rectangle is empty or its coordinates exceed "
                    "the supported integer range");
            }
            if (input.outputDirectory.empty()) {
                return fail(errorMessage, "Output directory is empty");
            }

            return true;
        }

        std::optional<cv::Point> getBlockPosition(
            const std::filesystem::path& filePath
        ) {
            static const std::regex BLOCK_FILENAME_PATTERN(
                R"(^rgb_x(-?[0-9]+)_y(-?[0-9]+)\.png$)"
            );

            const std::string filename = filePath.filename().string();
            std::smatch matchedParts;
            if (!std::regex_match(
                filename, matchedParts, BLOCK_FILENAME_PATTERN)) {
                return std::nullopt;
            }

            const std::string positionXText = matchedParts[1].str();
            const std::string positionYText = matchedParts[2].str();
            cv::Point position;

            const auto positionXResult = std::from_chars(
                positionXText.data(),
                positionXText.data() + positionXText.size(),
                position.x
            );
            const auto positionYResult = std::from_chars(
                positionYText.data(),
                positionYText.data() + positionYText.size(),
                position.y
            );

            if (positionXResult.ec != std::errc{} ||
                positionYResult.ec != std::errc{}) {
                return std::nullopt;
            }

            return position;
        }

        std::uint32_t getUnsignedInteger(
            const std::array<unsigned char, PNG_HEADER_SIZE>& header,
            std::size_t offset
        ) {
            std::uint32_t value = 0;
            constexpr std::size_t INTEGER_BYTE_COUNT = 4;

            for (std::size_t byteIndex = 0;
                byteIndex < INTEGER_BYTE_COUNT; ++byteIndex) {
                value = (value << BYTE_BIT_COUNT) | header[offset + byteIndex];
            }

            return value;
        }

        bool readPngHeader(
            const std::filesystem::path& path,
            std::array<unsigned char, PNG_HEADER_SIZE>& header,
            std::string& errorMessage
        ) {
            std::ifstream stream(path, std::ios::binary);
            if (!stream.read(
                reinterpret_cast<char*>(header.data()),
                static_cast<std::streamsize>(header.size()))) {
                return fail(errorMessage,
                    "Cannot read PNG header: " + path.string());
            }

            const bool hasSignature = std::equal(
                PNG_SIGNATURE.begin(), PNG_SIGNATURE.end(), header.begin());
            const bool hasHeaderMarker = std::equal(
                PNG_HEADER_MARKER.begin(), PNG_HEADER_MARKER.end(),
                header.begin() +
                static_cast<std::ptrdiff_t>(PNG_SIGNATURE.size()));

            if (!hasSignature || !hasHeaderMarker) {
                return fail(errorMessage,
                    "Invalid PNG header: " + path.string());
            }

            return true;
        }

        bool readPngSize(
            const std::filesystem::path& path,
            std::size_t fileIndex,
            cv::Size& size,
            std::string& errorMessage
        ) {
            std::array<unsigned char, PNG_HEADER_SIZE> header{};
            if (!readPngHeader(path, header, errorMessage)) {
                return false;
            }

            const auto width = getUnsignedInteger(header, PNG_WIDTH_OFFSET);
            const auto height = getUnsignedInteger(header, PNG_HEIGHT_OFFSET);
            const auto maximum =
                static_cast<std::uint32_t>(std::numeric_limits<int>::max());

            if (width == 0 || height == 0 ||
                width > maximum || height > maximum) {
                return fail(errorMessage,
                    "Unsupported PNG dimensions: " + path.string());
            }

            const unsigned char expectedDepth =
                fileIndex == FILE_COUNT - 1 ? MASK_DEPTH : COLOR_DEPTH;
            const unsigned char expectedColorType =
                fileIndex == 0 ? RGB_COLOR_TYPE : GRAYSCALE_COLOR_TYPE;

            if (header[PNG_DEPTH_OFFSET] != expectedDepth ||
                header[PNG_COLOR_TYPE_OFFSET] != expectedColorType) {
                return fail(errorMessage,
                    "Unexpected PNG depth or channel layout: " + path.string());
            }

            size = cv::Size(static_cast<int>(width), static_cast<int>(height));
            return true;
        }

        bool getBlockFiles(
            const std::filesystem::path& colorPath,
            BlockFiles& block,
            std::string& errorMessage
        ) {
            const auto position = getBlockPosition(colorPath);
            if (!position.has_value()) {
                return fail(errorMessage,
                    "Invalid color block filename: " + colorPath.string());
            }

            // Le suffixe original est gardé pour retrouver exactement
            // les fichiers associés, y compris si les nombres ont des zéros initiaux.
            const std::string suffix = colorPath.filename().string().substr(
                FILE_PREFIXES.front().size());
            const auto directory = colorPath.parent_path();

            for (std::size_t fileIndex = 0;
                fileIndex < FILE_COUNT; ++fileIndex) {
                block.paths[fileIndex] =
                    directory / (FILE_PREFIXES[fileIndex] + suffix);
            }

            block.rectangle = cv::Rect(*position, cv::Size());
            return true;
        }

        bool readBlock(
            const std::filesystem::path& colorPath,
            BlockFiles& block,
            std::string& errorMessage
        ) {
            if (!getBlockFiles(colorPath, block, errorMessage)) {
                return false;
            }

            cv::Size colorSize;
            for (std::size_t fileIndex = 0;
                fileIndex < FILE_COUNT; ++fileIndex) {
                cv::Size size;
                if (!readPngSize(
                    block.paths[fileIndex], fileIndex, size, errorMessage)) {
                    return false;
                }
                if (fileIndex == 0) {
                    colorSize = size;
                }
                if (size != colorSize) {
                    return fail(errorMessage,
                        "Block files have different dimensions: " +
                        colorPath.string());
                }
            }

            block.rectangle.width = colorSize.width;
            block.rectangle.height = colorSize.height;
            if (!hasSupportedCoordinates(block.rectangle)) {
                return fail(errorMessage,
                    "Block coordinates exceed the supported range: " +
                    colorPath.string());
            }

            return true;
        }

        bool collectBlocks(
            const DataNs::MosaicFiles& mosaic,
            std::vector<BlockFiles>& blocks,
            std::string& errorMessage
        ) {
            const std::filesystem::path directory(mosaic.mosaicPath);

            for (const auto& entry :
                std::filesystem::directory_iterator(directory)) {
                const std::string filename = entry.path().filename().string();
                if (filename.rfind("rgb_", 0) != 0) {
                    continue;
                }

                BlockFiles block;
                if (!readBlock(entry.path(), block, errorMessage)) {
                    return false;
                }
                blocks.push_back(std::move(block));
            }

            if (blocks.size() !=
                static_cast<std::size_t>(mosaic.totalBlocks)) {
                return fail(errorMessage,
                    "Mosaic block count mismatch: expected " +
                    std::to_string(mosaic.totalBlocks) + ", found " +
                    std::to_string(blocks.size()));
            }

            return true;
        }

        bool selectBlocks(
            const std::vector<BlockFiles>& blocks,
            const cv::Rect& rectangle,
            std::vector<CropBlock>& selectedBlocks,
            std::string& errorMessage
        ) {
            const std::int64_t expectedArea =
                static_cast<std::int64_t>(rectangle.width) * rectangle.height;
            std::int64_t coveredArea = 0;

            for (const auto& block : blocks) {
                const cv::Rect intersection = block.rectangle & rectangle;
                if (intersection.empty()) {
                    continue;
                }

                const std::int64_t area =
                    static_cast<std::int64_t>(intersection.width) *
                    intersection.height;
                if (area > expectedArea - coveredArea) {
                    return fail(errorMessage,
                        "Overlapping blocks cover the crop rectangle");
                }
                coveredArea += area;
                selectedBlocks.push_back({ block, intersection });
            }

            if (coveredArea != expectedArea) {
                return fail(errorMessage,
                    "Crop rectangle is outside the mosaic or contains gaps");
            }

            return true;
        }

        bool addCoverageInterval(
            const cv::Rect& rectangle,
            std::map<int, int>& bottomByTop,
            std::string& errorMessage
        ) {
            const int bottom = rectangle.y + rectangle.height;
            const auto nextInterval = bottomByTop.lower_bound(rectangle.y);

            if (nextInterval != bottomByTop.end() &&
                nextInterval->first < bottom) {
                return fail(errorMessage,
                    "Source blocks overlap inside the crop rectangle");
            }
            if (nextInterval != bottomByTop.begin() &&
                std::prev(nextInterval)->second > rectangle.y) {
                return fail(errorMessage,
                    "Source blocks overlap inside the crop rectangle");
            }

            bottomByTop.emplace(rectangle.y, bottom);
            return true;
        }

        bool validateCoverage(
            const std::vector<CropBlock>& blocks,
            std::string& errorMessage
        ) {
            std::vector<CoverageEvent> events;
            for (const auto& block : blocks) {
                const auto& rectangle = block.intersection;
                events.push_back({ rectangle.x, true, rectangle });
                events.push_back({
                    rectangle.x + rectangle.width, false, rectangle
                    });
            }

            // Les fins précèdent les débuts à la même abscisse :
            // deux blocs peuvent se toucher sans se chevaucher.
            std::sort(events.begin(), events.end(),
                [](const CoverageEvent& left, const CoverageEvent& right) {
                    if (left.positionX != right.positionX) {
                        return left.positionX < right.positionX;
                    }
                    return left.isStart < right.isStart;
                });

            std::map<int, int> bottomByTop;
            for (const auto& event : events) {
                if (!event.isStart) {
                    bottomByTop.erase(event.rectangle.y);
                    continue;
                }
                if (!addCoverageInterval(
                    event.rectangle, bottomByTop, errorMessage)) {
                    return false;
                }
            }

            return true;
        }

        std::string getOutputSuffix(const cv::Point& position) {
            return "_x" + std::to_string(position.x) +
                "_y" + std::to_string(position.y) + ".png";
        }

        bool writeCroppedFile(
            const CropBlock& block,
            std::size_t fileIndex,
            const std::filesystem::path& destination,
            std::string& errorMessage
        ) {
            const auto& sourcePath = block.source.paths[fileIndex];
            const cv::Mat image =
                cv::imread(sourcePath.string(), cv::IMREAD_UNCHANGED);

            if (image.empty() ||
                image.size() != block.source.rectangle.size() ||
                image.type() != IMAGE_TYPES[fileIndex]) {
                return fail(errorMessage,
                    "Cannot decode block with expected size and type: " +
                    sourcePath.string());
            }

            const cv::Rect localRectangle(
                block.intersection.x - block.source.rectangle.x,
                block.intersection.y - block.source.rectangle.y,
                block.intersection.width,
                block.intersection.height
            );

            // La vue reste valide jusqu'à la fin d'imwrite ; une copie
            // supplémentaire des pixels n'est donc pas nécessaire.
            if (!cv::imwrite(destination.string(), image(localRectangle))) {
                return fail(errorMessage,
                    "Cannot write cropped block: " + destination.string());
            }

            return true;
        }

        bool writeBlock(
            const CropBlock& block,
            const CropInput& input,
            std::string& errorMessage
        ) {
            const cv::Point position(
                block.intersection.x - input.rectangle.x,
                block.intersection.y - input.rectangle.y);
            const std::string suffix = getOutputSuffix(position);
            const bool isWholeBlock =
                block.intersection == block.source.rectangle;

            for (std::size_t fileIndex = 0;
                fileIndex < FILE_COUNT; ++fileIndex) {
                const auto destination = input.outputDirectory /
                    (FILE_PREFIXES[fileIndex] + suffix);

                if (isWholeBlock) {
                    std::filesystem::copy_file(
                        block.source.paths[fileIndex], destination);
                    continue;
                }
                if (!writeCroppedFile(
                    block, fileIndex, destination, errorMessage)) {
                    return false;
                }
            }

            return true;
        }

        bool createOutputDirectory(
            const std::filesystem::path& directory,
            std::string& errorMessage
        ) {
            std::error_code directoryError;
            const bool wasCreated =
                std::filesystem::create_directory(directory, directoryError);

            if (!wasCreated || directoryError) {
                return fail(errorMessage,
                    "Cannot create new output directory '" +
                    directory.string() + "': " +
                    (directoryError ? directoryError.message() :
                        "directory already exists"));
            }

            return true;
        }

        void removeIncompleteOutput(
            const std::filesystem::path& directory,
            std::string& errorMessage
        ) {
            std::error_code cleanupError;
            std::filesystem::remove_all(directory, cleanupError);

            if (cleanupError) {
                errorMessage +=
                    "; cannot remove incomplete output directory '" +
                    directory.string() + "': " + cleanupError.message();
            }
        }

        namespace PreparationModuleNs {

            struct PreparedCrop {

                std::vector<BlockFiles> blocks;
                std::vector<CropBlock> selectedBlocks;

            };

            bool prepare(
                const CropInput& input,
                PreparedCrop& prepared,
                std::string& errorMessage
            ) {
                return validateInput(input, errorMessage) &&
                    collectBlocks(input.mosaic, prepared.blocks, errorMessage) &&
                    selectBlocks(
                        prepared.blocks,
                        input.rectangle,
                        prepared.selectedBlocks,
                        errorMessage);
            }

        }

        namespace CoverageModuleNs {

            bool verify(
                const std::vector<CropBlock>& selectedBlocks,
                std::string& errorMessage
            ) {
                return validateCoverage(selectedBlocks, errorMessage);
            }

        }

        namespace WritingModuleNs {

            bool write(
                const CropInput& input,
                const std::vector<CropBlock>& selectedBlocks,
                DataNs::MosaicFiles& output,
                std::string& errorMessage,
                bool& hasCreatedDirectory
            ) {
                if (!createOutputDirectory(
                    input.outputDirectory, errorMessage)) {
                    return false;
                }
                hasCreatedDirectory = true;

                for (const auto& block : selectedBlocks) {
                    if (!writeBlock(block, input, errorMessage)) {
                        removeIncompleteOutput(
                            input.outputDirectory, errorMessage);
                        return false;
                    }
                }

                output.mosaicPath = input.outputDirectory.string();
                output.totalBlocks = static_cast<int>(selectedBlocks.size());
                return true;
            }

        }

    }

    bool cropMosaic(
        const CropInput& input,
        DataNs::MosaicFiles& output,
        std::string& errorMessage
    ) {
        // Copie des descriptions uniquement : protège aussi le cas où
        // output désigne input.mosaic.
        const CropInput request = input;
        output = {};
        errorMessage.clear();
        bool hasCreatedDirectory = false;

        try {
            PreparationModuleNs::PreparedCrop prepared;
            if (!PreparationModuleNs::prepare(
                request, prepared, errorMessage)) {
                return false;
            }

            if (!CoverageModuleNs::verify(
                prepared.selectedBlocks, errorMessage)) {
                return false;
            }

            return WritingModuleNs::write(
                request,
                prepared.selectedBlocks,
                output,
                errorMessage,
                hasCreatedDirectory);
        }
        catch (const std::exception& exception) {
            output = {};
            errorMessage = "Cannot crop mosaic: " +
                std::string(exception.what());
            if (hasCreatedDirectory) {
                removeIncompleteOutput(request.outputDirectory, errorMessage);
            }
            return false;
        }
    }

}