#include "Backend/core/PyramidalTiffWriter.h"

#include <opencv2/imgproc.hpp>
#include <tiffio.h>

#include <algorithm>
#include <cstdint>
#include <system_error>

namespace stitching {

namespace {

constexpr uint32_t TILE_SIZE = 512;
constexpr int PYRAMID_LEVELS = 4;

bool setDirectoryFields(TIFF* file, const cv::Mat& level, std::string& error) {
    const int depth = level.depth();
    const int channels = level.channels();
    const int bitsPerSample = depth == CV_8U ? 8 : depth == CV_16U ? 16 : 0;

    if (bitsPerSample == 0 || (channels != 1 && channels != 3 && channels != 4)) {
        error = "only CV_8U/CV_16U images with 1, 3, or 4 channels are supported";
        return false;
    }

    const uint16_t photometric =
        channels == 1 ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB;
    if (!TIFFSetField(file, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(level.cols)) ||
        !TIFFSetField(file, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(level.rows)) ||
        !TIFFSetField(file, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(channels)) ||
        !TIFFSetField(file, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(bitsPerSample)) ||
        !TIFFSetField(file, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) ||
        !TIFFSetField(file, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
        !TIFFSetField(file, TIFFTAG_PHOTOMETRIC, photometric) ||
        !TIFFSetField(file, TIFFTAG_COMPRESSION, COMPRESSION_LZW) ||
        !TIFFSetField(file, TIFFTAG_TILEWIDTH, TILE_SIZE) ||
        !TIFFSetField(file, TIFFTAG_TILELENGTH, TILE_SIZE)) {
        error = "cannot configure TIFF directory";
        return false;
    }

    if (channels == 4 &&
        !TIFFSetField(file, TIFFTAG_EXTRASAMPLES, 1, static_cast<uint16_t>(EXTRASAMPLE_UNASSALPHA))) {
        error = "cannot configure TIFF alpha channel";
        return false;
    }

    return true;
}

bool writeLevel(TIFF* file, const cv::Mat& level, std::string& error) {
    const size_t tileBytes = static_cast<size_t>(TILE_SIZE) * TILE_SIZE *
                             level.elemSize();
    cv::Mat tile = cv::Mat::zeros(
        static_cast<int>(TILE_SIZE),
        static_cast<int>(TILE_SIZE),
        level.type());

    for (int y = 0; y < level.rows; y += static_cast<int>(TILE_SIZE)) {
        for (int x = 0; x < level.cols; x += static_cast<int>(TILE_SIZE)) {
            tile.setTo(cv::Scalar::all(0));
            const cv::Rect sourceRect{
                x,
                y,
                std::min(static_cast<int>(TILE_SIZE), level.cols - x),
                std::min(static_cast<int>(TILE_SIZE), level.rows - y)
            };
            level(sourceRect).copyTo(tile(cv::Rect{
                0, 0, sourceRect.width, sourceRect.height
            }));

            const ttile_t tileIndex = TIFFComputeTile(
                file, static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0, 0);
            if (TIFFWriteEncodedTile(
                    file, tileIndex, tile.data, static_cast<tmsize_t>(tileBytes)) == -1) {
                error = "cannot write TIFF tile";
                return false;
            }
        }
    }

    if (!TIFFWriteDirectory(file)) {
        error = "cannot finalize TIFF directory";
        return false;
    }
    return true;
}

} // namespace

bool createPyramidalTiledTiff(
    const cv::Mat& image,
    const std::filesystem::path& outputPath,
    std::string& error
) {
    error.clear();
    if (image.empty()) {
        error = "input image is empty";
        return false;
    }
    if (image.dims != 2 || image.cols <= 0 || image.rows <= 0) {
        error = "input image must be a non-empty two-dimensional matrix";
        return false;
    }

    std::error_code directoryError;
    const auto parent = outputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directoryError);
        if (directoryError) {
            error = "cannot create output directory: " + directoryError.message();
            return false;
        }
    }

    TIFF* file = TIFFOpen(outputPath.string().c_str(), "w");
    if (file == nullptr) {
        error = "cannot open output TIFF";
        return false;
    }

    bool success = false;
    do {
        toff_t subIfdOffsets[PYRAMID_LEVELS - 1] = {};
        if (!TIFFSetField(file, TIFFTAG_SUBIFD, PYRAMID_LEVELS - 1, subIfdOffsets)) {
            error = "cannot configure TIFF pyramid directories";
            break;
        }

        if (!setDirectoryFields(file, image, error) || !writeLevel(file, image, error)) {
            break;
        }

        cv::Mat previous = image;
        for (int levelIndex = 1; levelIndex < PYRAMID_LEVELS; ++levelIndex) {
            cv::Mat level;
            cv::resize(previous, level, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
            if (level.empty()) {
                error = "cannot create reduced pyramid level";
                break;
            }
            if (!setDirectoryFields(file, level, error) || !writeLevel(file, level, error)) {
                break;
            }
            previous = std::move(level);
        }

        success = error.empty();
    } while (false);

    TIFFClose(file);
    if (!success) {
        std::error_code removeError;
        std::filesystem::remove(outputPath, removeError);
    }
    return success;
}

} // namespace stitching
