// tests/core/test_tile_pixel_reader.cpp
// Où vivent les pixels d'une tuile : le niveau 0 dans l'image de l'œuvre quand celle-ci sait servir une région, sinon
// dans le cache. La sonde décide image par image, et TilePixelReaderClass lit sans que l'appelant ait à le savoir.
#include "core/io/tile_pixel_reader.h"

#include "core/data/tile.h"
#include "core/io/pyramid_cache.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/pyramidal_tiff_writer.h"
#include "core/io/unicode_path.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/types.hpp>
#include <tiff.h>
#include <tiffio.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

    namespace fs = std::filesystem;

    constexpr int SOURCE_WIDTH = 240;
    // Plus haute que le seuil de la sonde (256 lignes par bande) : une seule bande compressée doit être jugée
    // trop coûteuse à lire par régions. Avec une image courte, une bande unique passerait pour une bande courte.
    constexpr int SOURCE_HEIGHT = 300;
    constexpr std::uint32_t SINGLE_STRIP = static_cast<std::uint32_t>(SOURCE_HEIGHT);
    constexpr std::uint32_t SHORT_STRIP_ROWS = 8;
    // Trois valeurs distinctes : un canal échangé se voit, à n'importe quel niveau.
    constexpr std::uint16_t RED_LEVEL = 30;
    constexpr std::uint16_t GREEN_LEVEL = 20;
    constexpr std::uint16_t BLUE_LEVEL = 10;

    struct TestDirectory {
        fs::path path;
        TestDirectory() {
            const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / ("tile-pixel-reader-test-" + std::to_string(stamp));
            fs::create_directories(path);
        }
        TestDirectory(const TestDirectory&) = delete;
        TestDirectory& operator=(const TestDirectory&) = delete;
        ~TestDirectory() {
            std::error_code error;
            fs::remove_all(path, error);
        }
    };

    // Couleur unie en RGB : c'est l'ordre que le fichier déclare, et celui que le cache doit conserver à tous ses
    // niveaux (le niveau 0 vient de ce fichier, les niveaux réduits du cache : les deux doivent concorder).
    cv::Mat makeColourImage() {
        return {SOURCE_HEIGHT, SOURCE_WIDTH, CV_16UC3, cv::Scalar(RED_LEVEL, GREEN_LEVEL, BLUE_LEVEL)};
    }

    // Écrit une image à bandes, comme un TIFF d'acquisition. `image` est écrit tel quel, et déclaré RGB.
    void writeStripTiff(const fs::path& path, const cv::Mat& image, std::uint16_t compression,
                        std::uint32_t rowsPerStrip) {
        TIFF* file = IONs::openTiff(path, "w");
        ASSERT_NE(file, nullptr);
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_IMAGEWIDTH, static_cast<std::uint32_t>(image.cols)));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_IMAGELENGTH, static_cast<std::uint32_t>(image.rows)));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_SAMPLESPERPIXEL, static_cast<std::uint16_t>(image.channels())));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_BITSPERSAMPLE, static_cast<std::uint16_t>(16)));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_COMPRESSION, compression));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_ROWSPERSTRIP, rowsPerStrip));
        for (int row = 0; row < image.rows; ++row) {
            void* rowData = const_cast<void*>(static_cast<const void*>(image.ptr(row)));
            ASSERT_NE(TIFFWriteScanline(file, rowData, static_cast<std::uint32_t>(row), 0), -1);
        }
        TIFFClose(file);
    }

    IONs::PyramidCacheEntry prepare(const fs::path& source, const fs::path& cacheDirectory) {
        IONs::PyramidCacheEntry entry;
        std::string errorMessage;
        EXPECT_TRUE(IONs::preparePyramidCache(source, cacheDirectory, entry, errorMessage)) << errorMessage;
        return entry;
    }

    int cachedLevelCount(const fs::path& pyramidPath) {
        return IONs::PyramidalTiffReaderClass(pyramidPath).getLevelCount();
    }

    void expectColour(const cv::Mat& pixels) {
        ASSERT_EQ(pixels.type(), CV_16UC3);
        ASSERT_FALSE(pixels.empty());
        const cv::Vec3w& channels = pixels.at<cv::Vec3w>(0, 0);
        EXPECT_EQ(channels[0], RED_LEVEL);
        EXPECT_EQ(channels[1], GREEN_LEVEL);
        EXPECT_EQ(channels[2], BLUE_LEVEL);
    }

}

// Une source non compressée sait servir une région : le cache ne garde que les niveaux réduits, et le niveau 0 est lu
// dans l'image de l'œuvre. Les deux doivent se recoller : mêmes dimensions annoncées, même ordre de canaux.
TEST(TilePixelReaderTest, UncompressedStripSourceKeepsLevelZeroOutOfTheCache) {
    const TestDirectory directory;
    const fs::path source = directory.path / "oeuvre.tif";
    writeStripTiff(source, makeColourImage(), COMPRESSION_NONE, SINGLE_STRIP);

    const IONs::PyramidCacheEntry entry = prepare(source, IONs::pyramidCacheDirectory(directory.path));
    EXPECT_TRUE(entry.levelZeroInSource);
    EXPECT_EQ(entry.width, SOURCE_WIDTH);     // la pleine résolution, que le cache ne contient pas
    EXPECT_EQ(entry.height, SOURCE_HEIGHT);
    EXPECT_EQ(entry.type, CV_16UC3);
    EXPECT_EQ(cachedLevelCount(entry.pyramidPath), DataNs::LEVEL_COUNT - 1);

    const IONs::TilePixelReaderClass reader(source, entry.pyramidPath);
    EXPECT_TRUE(reader.readsLevelZeroFromSource());
    EXPECT_EQ(reader.getLevelCount(), DataNs::LEVEL_COUNT);

    int width = 0;
    int height = 0;
    std::string error;
    ASSERT_TRUE(reader.getLevelDimensions(0, width, height, error)) << error;
    EXPECT_EQ(width, SOURCE_WIDTH);
    EXPECT_EQ(height, SOURCE_HEIGHT);
    ASSERT_TRUE(reader.getLevelDimensions(1, width, height, error)) << error;
    EXPECT_EQ(width, SOURCE_WIDTH / 2);
    EXPECT_EQ(height, SOURCE_HEIGHT / 2);

    // Niveau 0 pris dans l'œuvre, niveau 1 dans le cache : le même pixel, dans le même ordre de canaux.
    cv::Mat fullResolution;
    ASSERT_TRUE(reader.readRegion(0, cv::Rect(SOURCE_WIDTH - 3, SOURCE_HEIGHT - 3, 2, 2), fullResolution, error))
        << error;
    expectColour(fullResolution);
    cv::Mat reduced;
    ASSERT_TRUE(reader.readRegion(1, cv::Rect(0, 0, 2, 2), reduced, error)) << error;
    expectColour(reduced);
}

// Une seule bande compressée : chaque région coûterait le décodage de toute l'image. Le cache garde donc son niveau 0.
TEST(TilePixelReaderTest, CompressedSingleStripSourceKeepsLevelZeroInTheCache) {
    const TestDirectory directory;
    const fs::path source = directory.path / "oeuvre.tif";
    writeStripTiff(source, makeColourImage(), COMPRESSION_ADOBE_DEFLATE, SINGLE_STRIP);

    EXPECT_FALSE(IONs::probeSource(source).servesRegionsCheaply);

    const IONs::PyramidCacheEntry entry = prepare(source, IONs::pyramidCacheDirectory(directory.path));
    EXPECT_FALSE(entry.levelZeroInSource);
    EXPECT_EQ(entry.width, SOURCE_WIDTH);
    EXPECT_EQ(cachedLevelCount(entry.pyramidPath), DataNs::LEVEL_COUNT);

    const IONs::TilePixelReaderClass reader(source, entry.pyramidPath);
    EXPECT_FALSE(reader.readsLevelZeroFromSource());
    EXPECT_EQ(reader.getLevelCount(), DataNs::LEVEL_COUNT);

    cv::Mat fullResolution;
    std::string error;
    ASSERT_TRUE(reader.readRegion(0, cv::Rect(0, 0, 2, 2), fullResolution, error)) << error;
    expectColour(fullResolution);   // le cache a converti l'ordre des canaux, comme avant
}

// Une source qui n'est pas un TIFF : la sonde répond non sans rien casser, et le cache garde son niveau 0.
TEST(TilePixelReaderTest, NonTiffSourceKeepsLevelZeroInTheCache) {
    const TestDirectory directory;
    const fs::path source = directory.path / "oeuvre.png";
    // cv::imwrite attend du BGR : l'image écrite a donc bien R, G, B aux valeurs attendues.
    ASSERT_TRUE(IONs::writeImage(source, cv::Mat(SOURCE_HEIGHT, SOURCE_WIDTH, CV_16UC3,
                                                 cv::Scalar(BLUE_LEVEL, GREEN_LEVEL, RED_LEVEL))));

    EXPECT_FALSE(IONs::probeSource(source).servesRegionsCheaply);

    const IONs::PyramidCacheEntry entry = prepare(source, IONs::pyramidCacheDirectory(directory.path));
    EXPECT_FALSE(entry.levelZeroInSource);
    EXPECT_EQ(cachedLevelCount(entry.pyramidPath), DataNs::LEVEL_COUNT);

    const IONs::TilePixelReaderClass reader(source, entry.pyramidPath);
    cv::Mat fullResolution;
    std::string error;
    ASSERT_TRUE(reader.readRegion(0, cv::Rect(0, 0, 2, 2), fullResolution, error)) << error;
    expectColour(fullResolution);
}

// La sonde, organisation par organisation.
TEST(TilePixelReaderTest, ProbeAnswersForEachOrganisation) {
    const TestDirectory directory;
    const cv::Mat image = makeColourImage();

    const fs::path tiled = directory.path / "tuile.tif";
    std::string error;
    ASSERT_TRUE(IONs::createPyramidalTiledTiff(image, tiled, error)) << error;
    EXPECT_TRUE(IONs::probeSource(tiled).servesRegionsCheaply);

    const fs::path uncompressed = directory.path / "non-compresse.tif";
    writeStripTiff(uncompressed, image, COMPRESSION_NONE, SINGLE_STRIP);
    const IONs::SourceRegionSupport support = IONs::probeSource(uncompressed);
    EXPECT_TRUE(support.servesRegionsCheaply);
    EXPECT_EQ(support.width, SOURCE_WIDTH);
    EXPECT_EQ(support.height, SOURCE_HEIGHT);

    const fs::path shortStrips = directory.path / "bandes-courtes.tif";
    writeStripTiff(shortStrips, image, COMPRESSION_ADOBE_DEFLATE, SHORT_STRIP_ROWS);
    EXPECT_TRUE(IONs::probeSource(shortStrips).servesRegionsCheaply);

    const fs::path oneStrip = directory.path / "une-bande.tif";
    writeStripTiff(oneStrip, image, COMPRESSION_ADOBE_DEFLATE, SINGLE_STRIP);
    EXPECT_FALSE(IONs::probeSource(oneStrip).servesRegionsCheaply);

    EXPECT_FALSE(IONs::probeSource(directory.path / "absent.tif").servesRegionsCheaply);
    EXPECT_FALSE(IONs::probeSource({}).servesRegionsCheaply);
}

// La mise en page du cache se lit dans le fichier : une provenance disparue (réécriture en cours ailleurs, arrêt
// brutal) ne doit pas faire interpréter les niveaux réduits comme la pleine résolution.
TEST(TilePixelReaderTest, LayoutIsReadFromTheFileNotFromTheProvenance) {
    const TestDirectory directory;
    const fs::path source = directory.path / "oeuvre.tif";
    writeStripTiff(source, makeColourImage(), COMPRESSION_NONE, SINGLE_STRIP);
    const IONs::PyramidCacheEntry entry = prepare(source, IONs::pyramidCacheDirectory(directory.path));
    ASSERT_TRUE(entry.levelZeroInSource);

    fs::path provenance = entry.pyramidPath;
    provenance += ".source.json";
    ASSERT_TRUE(fs::remove(provenance));

    const IONs::TilePixelReaderClass reader(source, entry.pyramidPath);
    EXPECT_TRUE(reader.readsLevelZeroFromSource());
    int width = 0;
    int height = 0;
    std::string error;
    ASSERT_TRUE(reader.getLevelDimensions(0, width, height, error)) << error;
    EXPECT_EQ(width, SOURCE_WIDTH);
}

// readTileLevel donne les mêmes pixels par le cache et par l'image de l'œuvre : c'est ce qui permet aux steps
// d'appariement de lire le niveau réduit du cache au lieu de lire la pleine résolution et de la réduire eux-mêmes.
// Réduire puis échanger les canaux, ou l'inverse, revient au même : INTER_AREA moyenne canal par canal.
TEST(TilePixelReaderTest, ReadTileLevelGivesTheSamePixelsFromTheCacheAndFromTheSource) {
    const TestDirectory directory;
    const fs::path source = directory.path / "oeuvre.tif";
    writeStripTiff(source, makeColourImage(), COMPRESSION_NONE, SINGLE_STRIP);
    const IONs::PyramidCacheEntry entry = prepare(source, IONs::pyramidCacheDirectory(directory.path));

    for (int level = 0; level < DataNs::LEVEL_COUNT; ++level) {
        const cv::Mat fromCache = IONs::readTileLevel(source, entry.pyramidPath, level);
        const cv::Mat fromSource = IONs::readTileLevel(source, {}, level);   // tuile sans cache : le repli
        ASSERT_FALSE(fromCache.empty()) << "niveau " << level;
        ASSERT_EQ(fromCache.size(), fromSource.size()) << "niveau " << level;
        ASSERT_EQ(fromCache.type(), fromSource.type()) << "niveau " << level;
        EXPECT_EQ(cv::norm(fromCache, fromSource, cv::NORM_INF), 0.0) << "niveau " << level;
        expectColour(fromCache);   // toujours en RGB, quel que soit le chemin
    }
}

// Un niveau négatif ou absent ne doit pas faire lire la source entière par erreur.
TEST(TilePixelReaderTest, ReadTileLevelRefusesAnImpossibleLevel) {
    const TestDirectory directory;
    const fs::path source = directory.path / "oeuvre.tif";
    writeStripTiff(source, makeColourImage(), COMPRESSION_NONE, SINGLE_STRIP);
    const IONs::PyramidCacheEntry entry = prepare(source, IONs::pyramidCacheDirectory(directory.path));

    EXPECT_TRUE(IONs::readTileLevel(source, entry.pyramidPath, -1).empty());
    // Au-delà du dernier niveau, le cache échoue et le repli réduit autant de fois : l'image devient minuscule mais
    // reste lisible. Ce qui compte est qu'aucun chemin ne rende la pleine résolution par mégarde.
    const cv::Mat tooDeep = IONs::readTileLevel(source, entry.pyramidPath, DataNs::LEVEL_COUNT);
    if (!tooDeep.empty()) EXPECT_LT(tooDeep.cols, SOURCE_WIDTH);
}
