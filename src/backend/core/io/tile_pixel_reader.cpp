// src/backend/core/io/tile_pixel_reader.cpp
#include "core/io/tile_pixel_reader.h"

#include "core/data/tile.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/unicode_path.h"

#include <tiff.h>
#include <tiffio.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

namespace IONs {

    namespace fs = std::filesystem;

    namespace {

        // Au-delà, décoder une bande coûte plus que la région qu'on en tire : à la largeur d'une tuile du KIKIRPA,
        // 256 lignes de RGB 16 bits font déjà 17 Mio.
        constexpr std::uint32_t MAX_ROWS_PER_STRIP = 256;
        constexpr std::uint16_t BITS_PER_SAMPLE_8 = 8;
        constexpr std::uint16_t BITS_PER_SAMPLE_16 = 16;
        constexpr std::uint16_t CHANNEL_COUNT_GRAY = 1;
        constexpr std::uint16_t CHANNEL_COUNT_RGB = 3;
        constexpr std::uint16_t CHANNEL_COUNT_RGBA = 4;

        // Les mêmes conditions que PyramidalTiffReaderClass, plus le modèle colorimétrique : une palette, du YCbCr ou
        // du CMJN ne donnent pas des canaux utilisables tels quels, et le reader les rendrait sans le dire.
        bool hasReadableFormat(TIFF* tiffFile) {
            std::uint16_t bitsPerSample = 0;
            std::uint16_t samplesPerPixel = 0;
            std::uint16_t photometric = 0;
            if (!TIFFGetField(tiffFile, TIFFTAG_BITSPERSAMPLE, &bitsPerSample) ||
                !TIFFGetField(tiffFile, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel) ||
                !TIFFGetField(tiffFile, TIFFTAG_PHOTOMETRIC, &photometric))
                return false;

            std::uint16_t planarConfig = PLANARCONFIG_CONTIG;
            std::uint16_t sampleFormat = SAMPLEFORMAT_UINT;
            TIFFGetField(tiffFile, TIFFTAG_PLANARCONFIG, &planarConfig);
            TIFFGetField(tiffFile, TIFFTAG_SAMPLEFORMAT, &sampleFormat);

            return (bitsPerSample == BITS_PER_SAMPLE_8 || bitsPerSample == BITS_PER_SAMPLE_16) &&
                   (samplesPerPixel == CHANNEL_COUNT_GRAY || samplesPerPixel == CHANNEL_COUNT_RGB ||
                    samplesPerPixel == CHANNEL_COUNT_RGBA) &&
                   (photometric == PHOTOMETRIC_MINISBLACK || photometric == PHOTOMETRIC_RGB) &&
                   planarConfig == PLANARCONFIG_CONTIG && sampleFormat == SAMPLEFORMAT_UINT;
        }

        // Une région se lit-elle sans décoder toute l'image ? Voir le détail dans core/io/pyramidal_tiff_reader.cpp,
        // qui choisit le même chemin de lecture à partir des mêmes tags.
        bool servesRegionsCheaply(TIFF* tiffFile) {
            if (TIFFIsTiled(tiffFile) != 0) return true;

            std::uint16_t compression = COMPRESSION_NONE;
            std::uint32_t rowsPerStrip = 0;
            TIFFGetField(tiffFile, TIFFTAG_COMPRESSION, &compression);
            if (compression == COMPRESSION_NONE && TIFFIsByteSwapped(tiffFile) == 0) return true;
            return TIFFGetField(tiffFile, TIFFTAG_ROWSPERSTRIP, &rowsPerStrip) != 0 && rowsPerStrip > 0 &&
                   rowsPerStrip <= MAX_ROWS_PER_STRIP;
        }

    }

    SourceRegionSupport probeSource(const fs::path& sourcePath) {
        SourceRegionSupport support;
        if (sourcePath.empty()) return support;
        // Un fichier qui n'est pas un TIFF (JPEG, PNG) n'ouvre pas : libtiff l'écrit sur la sortie d'erreur et on
        // répond simplement « non », le cache gardera son niveau 0.
        TIFF* tiffFile = openTiff(sourcePath, "r");
        if (tiffFile == nullptr) return support;

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        const auto maximum = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
        if (TIFFGetField(tiffFile, TIFFTAG_IMAGEWIDTH, &width) != 0 &&
            TIFFGetField(tiffFile, TIFFTAG_IMAGELENGTH, &height) != 0 && width > 0 && height > 0 &&
            width <= maximum && height <= maximum && hasReadableFormat(tiffFile) && servesRegionsCheaply(tiffFile)) {
            support.servesRegionsCheaply = true;
            support.width = static_cast<int>(width);
            support.height = static_cast<int>(height);
        }
        TIFFClose(tiffFile);
        return support;
    }

    // La mise en page du cache se lit dans le FICHIER, et non dans sa provenance : celle-ci peut manquer un
    // instant (réécriture en cours dans un autre processus, arrêt brutal), et un fichier mal interprété rendrait
    // des pixels de la mauvaise échelle **sans aucune erreur**. Un cache complet a DataNs::LEVEL_COUNT niveaux ;
    // un cache dont le niveau 0 est resté dans l'œuvre en a un de moins. Changer LEVEL_COUNT oblige donc à
    // incrémenter PYRAMID_FORMAT_VERSION, ce qui réécrit les caches (voir core/io/pyramid_cache.cpp).
    bool TilePixelReaderClass::decideLevelZeroFromSource() const {
        if (sourcePath.empty()) return false;
        return PyramidalTiffReaderClass(pyramidPath).getLevelCount() == DataNs::LEVEL_COUNT - 1;
    }

    TilePixelReaderClass::TilePixelReaderClass(fs::path source, fs::path pyramid)
        : sourcePath(std::move(source)), pyramidPath(std::move(pyramid)) {
        levelZeroFromSource = decideLevelZeroFromSource();
    }

    TilePixelReaderClass::TilePixelReaderClass(fs::path source, fs::path pyramid, bool levelZeroInSource)
        : sourcePath(std::move(source)), pyramidPath(std::move(pyramid)) {
        levelZeroFromSource = levelZeroInSource && !sourcePath.empty();
    }

    fs::path TilePixelReaderClass::fileOf(int level) const {
        return level == 0 && levelZeroFromSource ? sourcePath : pyramidPath;
    }

    // Le fichier de cache commence au niveau 1 de la pyramide quand le niveau 0 vit dans l'image de l'œuvre.
    int TilePixelReaderClass::fileLevelOf(int level) const {
        if (level == 0) return 0;
        return levelZeroFromSource ? level - 1 : level;
    }

    bool TilePixelReaderClass::readRegion(int level, const cv::Rect& region, cv::Mat& output,
                                          std::string& errorMessage) const {
        const PyramidalTiffReaderClass reader(fileOf(level));
        return reader.readRegion(fileLevelOf(level), region, output, errorMessage);
    }

    bool TilePixelReaderClass::readLevel(int level, cv::Mat& output, std::string& errorMessage) const {
        const PyramidalTiffReaderClass reader(fileOf(level));
        return reader.readLevel(fileLevelOf(level), output, errorMessage);
    }

    bool TilePixelReaderClass::getLevelDimensions(int level, int& width, int& height,
                                                  std::string& errorMessage) const {
        const PyramidalTiffReaderClass reader(fileOf(level));
        return reader.getLevelDimensions(fileLevelOf(level), width, height, errorMessage);
    }

    int TilePixelReaderClass::getLevelCount() const {
        const int cachedLevels = PyramidalTiffReaderClass(pyramidPath).getLevelCount();
        return cachedLevels + (levelZeroFromSource ? 1 : 0);
    }

}
