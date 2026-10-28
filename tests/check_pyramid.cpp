#include "Backend/core/PyramidalTiffWriter.h"

#include <tiffio.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <string>
#include <vector>

namespace {

struct Level {
    uint32_t width = 0;
    uint32_t height = 0;
};

bool readLevel(TIFF* file, Level& level, std::string& error) {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t tileWidth = 0;
    uint32_t tileHeight = 0;

    if (!TIFFGetField(file, TIFFTAG_IMAGEWIDTH, &width)
        || !TIFFGetField(file, TIFFTAG_IMAGELENGTH, &height)) {
        error = "dimensions are missing";
        return false;
    }
    if (!TIFFIsTiled(file)
        || !TIFFGetField(file, TIFFTAG_TILEWIDTH, &tileWidth)
        || !TIFFGetField(file, TIFFTAG_TILELENGTH, &tileHeight)
        || tileWidth != 512
        || tileHeight != 512) {
        error = "level is not tiled with 512x512 tiles";
        return false;
    }

    level = {width, height};
    return true;
}

bool isHalfSize(const Level& previous, const Level& current) {
    const auto expectedWidth = previous.width / 2;
    const auto expectedHeight = previous.height / 2;
    return current.width == expectedWidth && current.height == expectedHeight;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <image_source> [image_pyramid.tif]\n";
        return 2;
    }

    const std::filesystem::path sourcePath = argv[1];
    const std::filesystem::path outputPath = argc == 3
        ? std::filesystem::path(argv[2])
        : sourcePath.parent_path() / ".cache" / "pyramids"
            / (sourcePath.stem().string() + "_pyramid.tif");

    const cv::Mat image = cv::imread(sourcePath.string(), cv::IMREAD_UNCHANGED);
    if (image.empty()) {
        std::cerr << "Cannot read source image: " << sourcePath << '\n';
        return 1;
    }

    std::string creationError;
    if (!stitching::createPyramidalTiledTiff(image, outputPath, creationError)) {
        std::cerr << "Cannot create pyramid: " << creationError << '\n';
        return 1;
    }

    TIFF* file = TIFFOpen(outputPath.string().c_str(), "r");
    if (file == nullptr) {
        std::cerr << "Cannot open generated TIFF: " << outputPath << '\n';
        return 1;
    }

    std::string error;
    Level level;
    bool valid = readLevel(file, level, error);

    uint16_t subIfdCount = 0;
    toff_t* subIfdOffsets = nullptr;
    if (valid && (!TIFFGetField(file, TIFFTAG_SUBIFD, &subIfdCount, &subIfdOffsets)
                  || subIfdOffsets == nullptr)) {
        error = "TIFF has no pyramid subdirectories";
        valid = false;
    }

    std::vector<Level> levels;
    if (valid) {
        levels.push_back(level);
        const std::vector<toff_t> offsets(
            subIfdOffsets, subIfdOffsets + subIfdCount);
        for (uint16_t index = 0; index < subIfdCount; ++index) {
            if (!TIFFSetSubDirectory(file, offsets[index])) {
                error = "cannot read pyramid level " + std::to_string(index + 1);
                valid = false;
                break;
            }
            if (!readLevel(file, level, error)) {
                valid = false;
                break;
            }
            levels.push_back(level);
        }
    }

    if (valid && levels.size() != 4) {
        error = "expected 4 pyramid levels, found " + std::to_string(levels.size());
        valid = false;
    }
    if (valid) {
        for (std::size_t index = 1; index < levels.size(); ++index) {
            if (!isHalfSize(levels[index - 1], levels[index])) {
                error = "level " + std::to_string(index)
                    + " is not half the size of the previous level";
                valid = false;
                break;
            }
        }
    }

    TIFFClose(file);

    if (!valid) {
        std::cerr << "INVALID: " << error << '\n';
        return 1;
    }

    std::cout << "OK: created " << outputPath << " with " << levels.size() << " levels";
    for (std::size_t index = 0; index < levels.size(); ++index) {
        std::cout << " [" << index << ": "
                  << levels[index].width << "x" << levels[index].height << "]";
    }
    std::cout << '\n';
    return 0;
}
