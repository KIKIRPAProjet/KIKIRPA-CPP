// src/backend/algorithms/cropping/mosaic_crop.cpp
#include "algorithms/cropping/mosaic_crop.h"

#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/tiled_tiff_writer.h"
#include "core/io/unicode_path.h"

#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <tiff.h>
#include <tiffio.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include <atomic>
#include <chrono>
#include <fstream>
#include <utility>

namespace CroppingNs {

    namespace {

        namespace fs = std::filesystem;
        constexpr int BITS_PER_BYTE = 8;

        struct FileCropInput {
            fs::path source;
            fs::path destination;
            cv::Rect rectangle;
            int imageType = 0;
        };

        struct TiffHeader {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint16_t channels = 0;
            std::uint16_t bits = 0;
            std::uint16_t planar = 0;
            std::uint16_t sampleFormat = SAMPLEFORMAT_UINT;
        };

        bool fail(std::string& errorMessage, const std::string& message) {
            errorMessage = message;
            return false;
        }

        bool validateRectangle(const CropInput& input, std::string& errorMessage) {
            const cv::Size size = input.mosaic.canvasSize;
            const cv::Rect& rectangle = input.rectangle;
            // Soustraire après contrôle du signe évite x + width et son débordement.
            if (size.width <= 0 || size.height <= 0 || rectangle.x < 0 || rectangle.y < 0 ||
                rectangle.width <= 0 || rectangle.height <= 0 ||
                rectangle.x > size.width || rectangle.y > size.height ||
                rectangle.width > size.width - rectangle.x ||
                rectangle.height > size.height - rectangle.y)
                return fail(errorMessage, "crop rectangle must be non-empty and inside the mosaic");
            return true;
        }

        bool readHeader(TIFF* file, TiffHeader& header) {
            TIFFGetFieldDefaulted(file, TIFFTAG_SAMPLEFORMAT, &header.sampleFormat);
            return TIFFIsTiled(file) &&
                TIFFGetField(file, TIFFTAG_IMAGEWIDTH, &header.width) &&
                TIFFGetField(file, TIFFTAG_IMAGELENGTH, &header.height) &&
                TIFFGetField(file, TIFFTAG_SAMPLESPERPIXEL, &header.channels) &&
                TIFFGetField(file, TIFFTAG_BITSPERSAMPLE, &header.bits) &&
                TIFFGetField(file, TIFFTAG_PLANARCONFIG, &header.planar);
        }

        bool validateSource(const FileCropInput& input, cv::Size canvasSize,
            std::string& errorMessage) {
            using TiffHandle = std::unique_ptr<TIFF, decltype(&TIFFClose)>;
            const TiffHandle file(IONs::openTiff(input.source, "r"), &TIFFClose);
            if (!file) return fail(errorMessage, "cannot open " + IONs::pathToUtf8(input.source));
            TiffHeader header;
            const int expectedBits = input.imageType == CV_8UC1 ? BITS_PER_BYTE : 2 * BITS_PER_BYTE;
            const int expectedChannels = input.imageType == CV_16UC3 ? 3 : 1;
            if (!readHeader(file.get(), header) ||
                header.width != static_cast<std::uint32_t>(canvasSize.width) ||
                header.height != static_cast<std::uint32_t>(canvasSize.height) ||
                header.bits != expectedBits || header.channels != expectedChannels ||
                header.planar != PLANARCONFIG_CONTIG || header.sampleFormat != SAMPLEFORMAT_UINT)
                return fail(errorMessage, "invalid mosaic TIFF dimensions or type: " +
                    IONs::pathToUtf8(input.source));
            return true;
        }

        // Le résultat est préparé séparément. Seul un ancien résultat identifié,
        // sans fichiers étrangers, peut être remplacé.
        class OutputDirectoryClass {

        public:
            OutputDirectoryClass() = default;
            OutputDirectoryClass(const OutputDirectoryClass&) = delete;
            OutputDirectoryClass& operator=(const OutputDirectoryClass&) = delete;

            ~OutputDirectoryClass() {
                if (ownsWorkspace && !preserveBackup) {
                    std::error_code ignored;
                    fs::remove_all(workspace, ignored);
                }
            }

            bool create(const fs::path& path, std::string& errorMessage) {
                if (path.empty())
                    return fail(errorMessage, "output directory is required");

                destination = fs::absolute(path).lexically_normal();
                if (destination.filename().empty())
                    destination = destination.parent_path();

                if (!fs::is_directory(destination.parent_path()))
                    return fail(errorMessage, "output parent directory does not exist");

                if (!validateDestination(errorMessage)) return false;
                if (!createWorkspace(errorMessage)) return false;

                std::ofstream marker(getStagingDirectory() / OWNER_FILE);
                marker << OWNER_SIGNATURE << '\n';
                marker.close();
                if (!marker)
                    return fail(errorMessage, "cannot write crop ownership marker");

                return true;
            }

            fs::path getStagingDirectory() const {
                return workspace / "result";
            }

            bool commit(std::string& errorMessage) {
                if (!validateDestination(errorMessage)) return false;

                const fs::path staging = getStagingDirectory();
                const fs::path backup = workspace / "previous";
                const bool hasPreviousResult = fs::exists(destination);
                std::error_code errorCode;

                if (hasPreviousResult) {
                    fs::rename(destination, backup, errorCode);
                    if (errorCode)
                        return fail(errorMessage,
                            "cannot back up previous crop: " + errorCode.message());

                    // Ne jamais nettoyer une sauvegarde avant publication
                    // ou restauration réussie.
                    preserveBackup = true;
                }

                fs::rename(staging, destination, errorCode);
                if (errorCode)
                    return restorePreviousResult(backup, errorCode, errorMessage);

                preserveBackup = false;
                return true;
            }

        private:
            static constexpr const char* OWNER_FILE = ".mosaic-crop-owner";
            static constexpr const char* OWNER_SIGNATURE = "KIKIRPA mosaic-crop v1";

            bool validateDestination(std::string& errorMessage) const {
                const auto status = fs::symlink_status(destination);
                if (!fs::exists(status)) return true;

                if (!fs::is_directory(status))
                    return fail(errorMessage,
                        "output must be a crop-owned directory, not a file or symbolic link");

                for (const auto& entry : fs::directory_iterator(destination)) {
                    const fs::path name = entry.path().filename();
                    const bool isExpectedName =
                        name == OWNER_FILE ||
                        name == "mosaic_image.tif" ||
                        name == "mosaic_provenance.tif" ||
                        name == "mosaic_mask.tif";

                    if (!isExpectedName || !fs::is_regular_file(entry.symlink_status()))
                        return fail(errorMessage,
                            "output directory contains foreign entries or symbolic links");
                }

                std::ifstream marker(destination / OWNER_FILE);
                std::string signature;
                if (!std::getline(marker, signature) || signature != OWNER_SIGNATURE)
                    return fail(errorMessage,
                        "output directory was not created by mosaic-crop");

                return true;
            }

            bool createWorkspace(std::string& errorMessage) {
                static std::atomic<unsigned int> sequence{ 0 };
                constexpr int MAXIMUM_ATTEMPTS = 100;
                const auto timestamp =
                    std::chrono::steady_clock::now().time_since_epoch().count();

                for (int attempt = 0; attempt < MAXIMUM_ATTEMPTS; ++attempt) {
                    workspace = destination.parent_path() /
                        (".mosaic-crop-" + std::to_string(timestamp) + "-" +
                            std::to_string(sequence.fetch_add(1)));

                    std::error_code errorCode;
                    ownsWorkspace = fs::create_directory(workspace, errorCode);
                    if (errorCode)
                        return fail(errorMessage,
                            "cannot create crop workspace: " + errorCode.message());

                    if (ownsWorkspace) {
                        if (!fs::create_directory(getStagingDirectory(), errorCode))
                            return fail(errorMessage,
                                "cannot create crop staging directory: " +
                                errorCode.message());
                        return true;
                    }
                }

                return fail(errorMessage, "cannot reserve a unique crop workspace");
            }

            bool restorePreviousResult(const fs::path& backup,
                const std::error_code& publicationError,
                std::string& errorMessage) {
                if (preserveBackup) {
                    std::error_code restoreError;
                    fs::rename(backup, destination, restoreError);
                    if (restoreError)
                        return fail(errorMessage,
                            "cannot publish crop: " + publicationError.message() +
                            "; cannot restore previous result: " + restoreError.message() +
                            "; backup preserved at " + IONs::pathToUtf8(backup));

                    preserveBackup = false;
                }

                return fail(errorMessage,
                    "cannot publish crop: " + publicationError.message());
            }

            fs::path destination;
            fs::path workspace;
            bool ownsWorkspace = false;
            bool preserveBackup = false;

        };

        DataNs::Mosaic makeOutput(const CropInput& input) {
            const fs::path directory = fs::absolute(input.outputDirectory);
            return { directory / "mosaic_image.tif", directory / "mosaic_provenance.tif",
                    directory / "mosaic_mask.tif", input.rectangle.size() };
        }

        std::array<FileCropInput, 3> makeRequests(const CropInput& input,
            const DataNs::Mosaic& output) {
            return { {{input.mosaic.imagePath, output.imagePath, input.rectangle, CV_16UC3},
                     {input.mosaic.provenancePath, output.provenancePath, input.rectangle, CV_16UC1},
                     {input.mosaic.maskPath, output.maskPath, input.rectangle, CV_8UC1}} };
        }

        bool writeBlockRow(const FileCropInput& input, int blockTop,
            IONs::TiledTiffWriterClass& writer, std::string& errorMessage) {
            const IONs::PyramidalTiffReaderClass reader(input.source);
            const int height = std::min(DataNs::BLOCK_SIZE, input.rectangle.height - blockTop);
            int blockLeft = 0;
            while (blockLeft < input.rectangle.width) {
                const int width = std::min(DataNs::BLOCK_SIZE, input.rectangle.width - blockLeft);
                const cv::Rect sourceArea(input.rectangle.x + blockLeft,
                    input.rectangle.y + blockTop, width, height);
                cv::Mat pixels;
                if (!reader.readRegion(0, sourceArea, pixels, errorMessage)) return false;
                if (pixels.type() != input.imageType)
                    return fail(errorMessage, "unexpected mosaic region type");
                // Le lecteur TIFF conserve l'ordre des échantillons : pas de conversion BGR/RGB.
                if (!writer.writeBlock({ blockLeft, blockTop }, pixels, errorMessage)) return false;
                blockLeft += width;
            }
            return true;
        }

        bool cropFile(const FileCropInput& input, std::string& errorMessage) {
            IONs::TiledTiffWriterClass writer;
            if (!writer.open(input.destination, input.rectangle.size(), input.imageType, errorMessage))
                return false;
            int blockTop = 0;
            while (blockTop < input.rectangle.height) {
                if (!writeBlockRow(input, blockTop, writer, errorMessage)) return false;
                blockTop += std::min(DataNs::BLOCK_SIZE, input.rectangle.height - blockTop);
            }
            return writer.close(errorMessage);
        }

        bool validateOutputLocation(const CropInput& input, std::string& errorMessage) {
            if (!fs::exists(input.outputDirectory)) return true;

            const std::array<fs::path, 3> sources = {
                input.mosaic.imagePath,
                input.mosaic.provenancePath,
                input.mosaic.maskPath
            };

            for (const auto& source : sources) {
                fs::path ancestor = fs::canonical(source).parent_path();

                while (!ancestor.empty()) {
                    if (fs::equivalent(ancestor, input.outputDirectory))
                        return fail(errorMessage,
                            "output directory must not contain source files");

                    const fs::path parent = ancestor.parent_path();
                    if (parent == ancestor) break;
                    ancestor = parent;
                }
            }

            return true;
        }

        bool cropValidated(const CropInput& input, DataNs::Mosaic& output,
            std::string& errorMessage) {
            if (!validateRectangle(input, errorMessage)) return false;
            if (input.outputDirectory.empty())
                return fail(errorMessage, "output directory is required");

            DataNs::Mosaic candidate = makeOutput(input);
            for (const auto& request : makeRequests(input, candidate)) {
                if (!validateSource(request, input.mosaic.canvasSize, errorMessage))
                    return false;
            }

            if (!validateOutputLocation(input, errorMessage)) return false;

            OutputDirectoryClass directory;
            if (!directory.create(input.outputDirectory, errorMessage)) return false;

            CropInput stagingInput = input;
            stagingInput.outputDirectory = directory.getStagingDirectory();
            const DataNs::Mosaic stagingOutput = makeOutput(stagingInput);

            for (const auto& request : makeRequests(input, stagingOutput)) {
                if (!cropFile(request, errorMessage)) return false;
            }

            if (!directory.commit(errorMessage)) return false;

            output = std::move(candidate);
            return true;
        }

    }

    bool cropMosaic(const CropInput& input, DataNs::Mosaic& output,
        std::string& errorMessage) {
        output = {};
        errorMessage.clear();
        try {
            return cropValidated(input, output, errorMessage);
        }
        catch (const std::exception& exception) {
            output = {};
            errorMessage = exception.what();
            return false;
        }
    }

}