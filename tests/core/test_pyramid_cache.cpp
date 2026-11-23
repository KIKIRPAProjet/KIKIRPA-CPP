// tests/core/test_pyramid_cache.cpp
// Cache des TIFF pyramidaux partagé par l'import et l'interface : nom propre à chaque source, validité par empreinte
// exacte de la source, réutilisation sans relire l'image.
#include "core/io/pyramid_cache.h"

#include "core/io/unicode_path.h"

#include <gtest/gtest.h>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

    namespace fs = std::filesystem;

    constexpr int IMAGE_SIZE = 16;
    constexpr int GREY_LEVEL = 100;
    constexpr std::chrono::hours ONE_DAY{24};   // recul de la date d'une source « plus ancienne »

    struct TestDirectory {
        fs::path path;
        TestDirectory() {
            const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / ("pyramid-cache-test-" + std::to_string(stamp));
            fs::create_directories(path);
        }
        TestDirectory(const TestDirectory&) = delete;
        TestDirectory& operator=(const TestDirectory&) = delete;
        TestDirectory(TestDirectory&&) = delete;
        TestDirectory& operator=(TestDirectory&&) = delete;
        ~TestDirectory() {
            std::error_code error;
            fs::remove_all(path, error);
        }
    };

    void writeGreyImage(const fs::path& path, int width) {
        ASSERT_TRUE(IONs::writeImage(path, cv::Mat(IMAGE_SIZE, width, CV_8UC1, cv::Scalar(GREY_LEVEL))));
    }

    IONs::PyramidCacheEntry prepare(const fs::path& source, const fs::path& cacheDirectory) {
        IONs::PyramidCacheEntry entry;
        std::string errorMessage;
        EXPECT_TRUE(IONs::preparePyramidCache(source, cacheDirectory, entry, errorMessage)) << errorMessage;
        return entry;
    }

}

TEST(PyramidCacheTest, NameDependsOnTheFullSourcePath) {
    const fs::path cache = "cache";
    const fs::path tif = IONs::pyramidCachePath(cache, "images/photo.tif");
    const fs::path tiff = IONs::pyramidCachePath(cache, "images/photo.tiff");
    const fs::path otherDirectory = IONs::pyramidCachePath(cache, "autres/photo.tif");

    EXPECT_NE(tif, tiff);
    EXPECT_NE(tif, otherDirectory);
    EXPECT_EQ(tif, IONs::pyramidCachePath(cache, "images/photo.tif"));   // stable
    EXPECT_EQ(tif.parent_path(), cache);
    EXPECT_EQ(IONs::pathToUtf8(tif.filename()).rfind("photo_", 0), 0U);   // le nom de l'image reste lisible
}

TEST(PyramidCacheTest, CreatesThenReusesAValidPyramid) {
    const TestDirectory directory;
    const fs::path source = directory.path / "a.tif";
    writeGreyImage(source, 2 * IMAGE_SIZE);
    const fs::path cache = IONs::pyramidCacheDirectory(directory.path);

    const IONs::PyramidCacheEntry created = prepare(source, cache);
    EXPECT_FALSE(created.reused);
    EXPECT_TRUE(IONs::isPyramidCacheValid(source, created.pyramidPath));

    const IONs::PyramidCacheEntry reused = prepare(source, cache);
    EXPECT_TRUE(reused.reused);
    EXPECT_EQ(reused.pyramidPath, created.pyramidPath);
    EXPECT_EQ(reused.width, 2 * IMAGE_SIZE);
    EXPECT_EQ(reused.height, IMAGE_SIZE);
    EXPECT_EQ(reused.type, CV_8UC1);
}

TEST(PyramidCacheTest, PyramidWithoutProvenanceIsInvalid) {
    const TestDirectory directory;
    const fs::path source = directory.path / "a.tif";
    writeGreyImage(source, IMAGE_SIZE);
    const IONs::PyramidCacheEntry entry = prepare(source, IONs::pyramidCacheDirectory(directory.path));

    fs::path provenance = entry.pyramidPath;
    provenance += ".source.json";
    ASSERT_TRUE(fs::remove(provenance));
    EXPECT_FALSE(IONs::isPyramidCacheValid(source, entry.pyramidPath));
}

// La date de la source doit être identique : un fichier plus ancien que la pyramide invalide aussi le cache.
TEST(PyramidCacheTest, SourceReplacedByAnOlderFileInvalidatesTheCache) {
    const TestDirectory directory;
    const fs::path source = directory.path / "a.tif";
    writeGreyImage(source, IMAGE_SIZE);
    const IONs::PyramidCacheEntry entry = prepare(source, IONs::pyramidCacheDirectory(directory.path));

    fs::last_write_time(source, fs::last_write_time(entry.pyramidPath) - ONE_DAY);
    EXPECT_FALSE(IONs::isPyramidCacheValid(source, entry.pyramidPath));
    EXPECT_FALSE(prepare(source, IONs::pyramidCacheDirectory(directory.path)).reused);
}

TEST(PyramidCacheTest, MissingSourceOrPyramidIsInvalid) {
    const TestDirectory directory;
    const fs::path source = directory.path / "a.tif";
    EXPECT_FALSE(IONs::isPyramidCacheValid(source, IONs::pyramidCachePath(directory.path, source)));

    IONs::PyramidCacheEntry entry;
    std::string errorMessage;
    EXPECT_FALSE(IONs::preparePyramidCache(source, directory.path, entry, errorMessage));
    EXPECT_FALSE(errorMessage.empty());
}
