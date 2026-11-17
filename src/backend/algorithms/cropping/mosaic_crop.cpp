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

        // Un dossier réservé à cet appel rend les trois fichiers solidaires : si
        // le dernier échoue, les premiers sont retirés aussi. Aucun dossier existant
        // n'est possédé ou supprimé par cette garde.
        class OutputDirectoryClass {

        public:
            ~OutputDirectoryClass() {
                if (owned && !committed) {
                    std::error_code ignored;
                    fs::remove_all(directory, ignored);
                }
            }

            OutputDirectoryClass() = default;
            OutputDirectoryClass(const OutputDirectoryClass&) = delete;
            OutputDirectoryClass& operator=(const OutputDirectoryClass&) = delete;

            bool create(const fs::path& path, std::string& errorMessage) {
                if (path.empty()) return fail(errorMessage, "output directory is required");
                directory = fs::absolute(path);
                if (!fs::is_directory(directory.parent_path()))
                    return fail(errorMessage, "output parent directory does not exist");
                std::error_code errorCode;
                owned = fs::create_directory(directory, errorCode);
                if (!owned) return fail(errorMessage, "cannot create a new output directory: " +
                    IONs::pathToUtf8(directory));
                return true;
            }

            void commit() { committed = true; }

        private:
            fs::path directory;
            bool owned = false;
            bool committed = false;

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

        bool cropValidated(const CropInput& input, DataNs::Mosaic& output,
            std::string& errorMessage) {
            if (!validateRectangle(input, errorMessage)) return false;
            if (input.outputDirectory.empty()) return fail(errorMessage, "output directory is required");
            const DataNs::Mosaic candidate = makeOutput(input);
            const auto requests = makeRequests(input, candidate);
            for (const auto& request : requests)
                if (!validateSource(request, input.mosaic.canvasSize, errorMessage)) return false;
            OutputDirectoryClass directory;
            if (!directory.create(input.outputDirectory, errorMessage)) return false;
            for (const auto& request : requests)
                if (!cropFile(request, errorMessage)) return false;
            output = candidate;
            directory.commit();
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