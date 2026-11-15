// src/backend/core/io/tiled_tiff_writer.cpp
#include "core/io/tiled_tiff_writer.h"

#include "core/data/tile.h"
#include "core/io/file_replacement.h"

#include <opencv2/core/cvdef.h>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <tiff.h>
#include <tiffio.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>

namespace IONs {

    namespace fs = std::filesystem;

    namespace {

        constexpr int BITS_PER_BYTE = 8;
        // Estimation prudente de la taille du fichier, pour choisir BigTIFF avant d'écrire : le LZW peut grossir
        // une image bruitée, chaque bloc a ses entrées dans les tables d'offsets, plus une marge pour l'en-tête.
        constexpr long double WORST_CASE_COMPRESSION_RATIO = 1.5L;
        constexpr long double BYTES_PER_BLOCK_ENTRY = 256.0L;
        constexpr long double HEADER_MARGIN_BYTES = 1024.0L * 1024.0L;

        bool isSupportedType(int imageType) {
            const int depth = CV_MAT_DEPTH(imageType);
            const int channelCount = CV_MAT_CN(imageType);
            return (depth == CV_8U || depth == CV_16U) && (channelCount == 1 || channelCount == 3);
        }

        // Nombre de blocs nécessaires pour couvrir `length` pixels (le dernier peut être incomplet).
        int blockCountAlong(int length) { return 1 + (length - 1) / DataNs::BLOCK_SIZE; }

        // Un TIFF classique est limité à 4 Gio (offsets sur 32 bits).
        bool shouldUseBigTiff(cv::Size imageSize, std::size_t bytesPerPixel) {
            const long double pixelCount = static_cast<long double>(imageSize.width) * imageSize.height;
            const long double blockCount =
                static_cast<long double>(blockCountAlong(imageSize.width)) * blockCountAlong(imageSize.height);
            const long double estimatedFileSize = pixelCount * static_cast<long double>(bytesPerPixel) *
                                                  WORST_CASE_COMPRESSION_RATIO +
                                                  blockCount * BYTES_PER_BLOCK_ENTRY + HEADER_MARGIN_BYTES;
            return estimatedFileSize >= std::numeric_limits<std::uint32_t>::max();
        }

        // Dimensions, canaux, bits par canal, origine en haut à gauche, canaux entrelacés (RGBRGB), modèle
        // colorimétrique, compression LZW et taille des blocs.
        bool configureTiff(TIFF* tiffFile, cv::Size imageSize, int imageType, std::string& errorMessage) {
            const auto channelCount = static_cast<std::uint16_t>(CV_MAT_CN(imageType));
            const auto bitsPerSample = static_cast<std::uint16_t>(CV_ELEM_SIZE1(imageType) * BITS_PER_BYTE);
            const std::uint16_t photometric = channelCount == 1 ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB;
            const auto blockSize = static_cast<std::uint32_t>(DataNs::BLOCK_SIZE);
            if (!TIFFSetField(tiffFile, TIFFTAG_IMAGEWIDTH, static_cast<std::uint32_t>(imageSize.width)) ||
                !TIFFSetField(tiffFile, TIFFTAG_IMAGELENGTH, static_cast<std::uint32_t>(imageSize.height)) ||
                !TIFFSetField(tiffFile, TIFFTAG_SAMPLESPERPIXEL, channelCount) ||
                !TIFFSetField(tiffFile, TIFFTAG_BITSPERSAMPLE, bitsPerSample) ||
                !TIFFSetField(tiffFile, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT) ||
                !TIFFSetField(tiffFile, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) ||
                !TIFFSetField(tiffFile, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
                !TIFFSetField(tiffFile, TIFFTAG_PHOTOMETRIC, photometric) ||
                !TIFFSetField(tiffFile, TIFFTAG_COMPRESSION, COMPRESSION_LZW) ||
                !TIFFSetField(tiffFile, TIFFTAG_TILEWIDTH, blockSize) ||
                !TIFFSetField(tiffFile, TIFFTAG_TILELENGTH, blockSize)) {
                errorMessage = "cannot configure TIFF image";
                return false;
            }
            return true;
        }

        bool validateImageFormat(cv::Size imageSize, int imageType, std::string& errorMessage) {
            if (imageSize.width <= 0 || imageSize.height <= 0) {
                errorMessage = "TIFF image size must be positive";
                return false;
            }
            if (!isSupportedType(imageType)) {
                errorMessage = "only CV_8U/CV_16U images with 1 or 3 channels are supported";
                return false;
            }
            return true;
        }

        bool createParentDirectory(const fs::path& outputPath, std::string& errorMessage) {
            const fs::path parentDirectory = outputPath.parent_path();
            if (parentDirectory.empty()) return true;
            std::error_code directoryError;
            fs::create_directories(parentDirectory, directoryError);
            if (!directoryError) return true;
            errorMessage = "cannot create directory '" + parentDirectory.string() + "': " + directoryError.message();
            return false;
        }

    }

    TiledTiffWriterClass::~TiledTiffWriterClass() { abandon(); }

    bool TiledTiffWriterClass::open(const fs::path& outputPath, cv::Size imageSize, int imageType,
                                    std::string& errorMessage) {
        if (tiffFile) {
            errorMessage = "TIFF writer is already open";
            return false;
        }
        if (!validateImageFormat(imageSize, imageType, errorMessage) ||
            !createParentDirectory(outputPath, errorMessage))
            return false;

        bigTiff = shouldUseBigTiff(imageSize, CV_ELEM_SIZE(imageType));
        temporaryPath = makeTemporaryPath(outputPath);
        tiffFile.reset(TIFFOpen(temporaryPath.string().c_str(), bigTiff ? "w8" : "w"));
        if (!tiffFile) {
            errorMessage = "cannot open output TIFF '" + temporaryPath.string() + "'";
            abandon();
            return false;
        }
        if (!configureTiff(tiffFile.get(), imageSize, imageType, errorMessage)) {
            abandon();
            return false;
        }
        destinationPath = outputPath;
        canvasSize = imageSize;
        pixelType = imageType;
        columnCount = blockCountAlong(canvasSize.width);
        tileBuffer = cv::Mat::zeros(DataNs::BLOCK_SIZE, DataNs::BLOCK_SIZE, pixelType);
        writtenBlocks.assign(static_cast<std::size_t>(columnCount) * blockCountAlong(canvasSize.height), false);
        return true;
    }

    bool TiledTiffWriterClass::validateBlock(cv::Point blockOrigin, const cv::Mat& block,
                                             std::string& errorMessage) const {
        if (blockOrigin.x < 0 || blockOrigin.y < 0 || blockOrigin.x >= canvasSize.width ||
            blockOrigin.y >= canvasSize.height || blockOrigin.x % DataNs::BLOCK_SIZE != 0 ||
            blockOrigin.y % DataNs::BLOCK_SIZE != 0) {
            errorMessage = "block origin must lie inside the image and be aligned to the block grid";
            return false;
        }
        const cv::Size expectedSize{std::min(DataNs::BLOCK_SIZE, canvasSize.width - blockOrigin.x),
                                    std::min(DataNs::BLOCK_SIZE, canvasSize.height - blockOrigin.y)};
        if (block.dims != 2 || block.type() != pixelType || block.size() != expectedSize) {
            errorMessage = "block has an unexpected type or size";
            return false;
        }
        if (writtenBlocks[blockIndex(blockOrigin)]) {
            errorMessage = "block at (" + std::to_string(blockOrigin.x) + ", " + std::to_string(blockOrigin.y) +
                           ") was already written";
            return false;
        }
        return true;
    }

    std::size_t TiledTiffWriterClass::blockIndex(cv::Point blockOrigin) const {
        return static_cast<std::size_t>(blockOrigin.y / DataNs::BLOCK_SIZE) * columnCount +
               static_cast<std::size_t>(blockOrigin.x / DataNs::BLOCK_SIZE);
    }

    bool TiledTiffWriterClass::writeBlock(cv::Point blockOrigin, const cv::Mat& block, std::string& errorMessage) {
        if (!tiffFile) {
            errorMessage = "TIFF writer is not open";
            return false;
        }
        if (!validateBlock(blockOrigin, block, errorMessage)) return false;
        // Les blocs de bord sont complétés par du noir : TIFF n'enregistre que des blocs entiers.
        if (block.size() != tileBuffer.size()) tileBuffer.setTo(cv::Scalar::all(0));
        block.copyTo(tileBuffer(cv::Rect{0, 0, block.cols, block.rows}));
        const ttile_t tileIndex = TIFFComputeTile(tiffFile.get(), static_cast<std::uint32_t>(blockOrigin.x),
                                                  static_cast<std::uint32_t>(blockOrigin.y), 0, 0);
        const auto tileBytes = static_cast<tmsize_t>(tileBuffer.total() * tileBuffer.elemSize());
        if (TIFFWriteEncodedTile(tiffFile.get(), tileIndex, tileBuffer.data, tileBytes) == -1) {
            errorMessage = "cannot write TIFF tile";
            return false;
        }
        writtenBlocks[blockIndex(blockOrigin)] = true;
        return true;
    }

    bool TiledTiffWriterClass::close(std::string& errorMessage) {
        if (!tiffFile) {
            errorMessage = "TIFF writer is not open";
            return false;
        }
        const auto missingBlockCount = std::count(writtenBlocks.begin(), writtenBlocks.end(), false);
        if (missingBlockCount > 0) {
            errorMessage = std::to_string(missingBlockCount) + " block(s) of the grid were not written";
            abandon();
            return false;
        }
        if (!TIFFWriteDirectory(tiffFile.get())) {
            errorMessage = "cannot finalize TIFF directory";
            abandon();
            return false;
        }
        tiffFile.reset();   // fermé avant le renommage : Windows ne renomme pas un fichier ouvert
        if (!replaceFile(temporaryPath, destinationPath, errorMessage)) {
            abandon();
            return false;
        }
        temporaryPath.clear();
        writtenBlocks.clear();
        return true;
    }

    void TiledTiffWriterClass::abandon() {
        tiffFile.reset();
        writtenBlocks.clear();
        if (temporaryPath.empty()) return;
        std::error_code ignoredError;   // rien à faire de plus si le temporaire ne peut pas être supprimé
        fs::remove(temporaryPath, ignoredError);
        temporaryPath.clear();
    }

}
