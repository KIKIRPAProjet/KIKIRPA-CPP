// tests/core/test_pyramid_cache.cpp
// Cache des TIFF pyramidaux partagé par l'import et l'interface : nom propre à chaque source, validité par empreinte
// exacte de la source et par version du format, réutilisation sans relire l'image, canaux dans l'ordre que le TIFF
// déclare (RGB, et non l'ordre BGR de cv::imread), et ménage : orphelines, périmées, ou tout le cache.
#include "core/io/pyramid_cache.h"

#include "core/io/file_fingerprint.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/unicode_path.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/types.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>

namespace {

    namespace fs = std::filesystem;

    constexpr int IMAGE_SIZE = 16;
    constexpr int GREY_LEVEL = 100;
    // Trois valeurs distinctes : un canal échangé se voit, contrairement à un gris.
    constexpr int BLUE_LEVEL = 10;
    constexpr int GREEN_LEVEL = 20;
    constexpr int RED_LEVEL = 30;
    constexpr int PYRAMID_FORMAT_VERSION = 2;   // même valeur que pyramid_cache.cpp : le format est un contrat
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

    // cv::imwrite attend du BGR : Scalar(bleu, vert, rouge) écrit donc dans le fichier un pixel dont le rouge vaut
    // RED_LEVEL. C'est cet ordre-là, celui de l'image de l'œuvre, que la pyramide doit rendre.
    void writeColourImage(const fs::path& path) {
        const cv::Mat image(IMAGE_SIZE, IMAGE_SIZE, CV_8UC3, cv::Scalar(BLUE_LEVEL, GREEN_LEVEL, RED_LEVEL));
        ASSERT_TRUE(IONs::writeImage(path, image));
    }

    fs::path provenanceOf(const fs::path& pyramidPath) {
        fs::path provenance = pyramidPath;
        provenance += ".source.json";
        return provenance;
    }

    nlohmann::json readProvenance(const fs::path& pyramidPath) {
        std::ifstream stream(provenanceOf(pyramidPath), std::ios::binary);
        return nlohmann::json::parse(stream, nullptr, false);
    }

    std::int64_t nowInSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Fait passer la pyramide pour employée il y a `days` jours : même horloge que pyramid_cache.cpp.
    void setLastReadDaysAgo(const fs::path& pyramidPath, int days) {
        nlohmann::json provenance = readProvenance(pyramidPath);
        ASSERT_TRUE(provenance.is_object());
        provenance["lastRead"] = nowInSeconds() - static_cast<std::int64_t>(days) * 24 * 60 * 60;
        std::ofstream stream(provenanceOf(pyramidPath), std::ios::binary);
        stream << provenance.dump();
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

    ASSERT_TRUE(fs::remove(provenanceOf(entry.pyramidPath)));
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

// Le TIFF du cache doit contenir ce qu'il déclare (PHOTOMETRIC_RGB), et non l'ordre BGR rendu par cv::imread : sans
// cela, le fichier mentait sur ses canaux et chaque lecteur défaisait l'erreur de son côté (dette T6/T7).
TEST(PyramidCacheTest, PyramidHoldsRgbInTheOrderItDeclares) {
    const TestDirectory directory;
    const fs::path source = directory.path / "couleur.tif";
    writeColourImage(source);

    const IONs::PyramidCacheEntry entry = prepare(source, IONs::pyramidCacheDirectory(directory.path));
    ASSERT_EQ(entry.type, CV_8UC3);

    const IONs::PyramidalTiffReaderClass reader(entry.pyramidPath);
    cv::Mat pixel;
    std::string errorMessage;
    ASSERT_TRUE(reader.readRegion(0, cv::Rect(0, 0, 1, 1), pixel, errorMessage)) << errorMessage;
    ASSERT_EQ(pixel.type(), CV_8UC3);

    const cv::Vec3b& channels = pixel.at<cv::Vec3b>(0, 0);
    EXPECT_EQ(channels[0], RED_LEVEL);
    EXPECT_EQ(channels[1], GREEN_LEVEL);
    EXPECT_EQ(channels[2], BLUE_LEVEL);
}

// La provenance décrit la source dans "source" et le fichier produit dans "formatVersion".
TEST(PyramidCacheTest, ProvenanceCarriesTheSourceAndTheFormatVersion) {
    const TestDirectory directory;
    const fs::path source = directory.path / "a.tif";
    writeGreyImage(source, IMAGE_SIZE);
    const IONs::PyramidCacheEntry entry = prepare(source, IONs::pyramidCacheDirectory(directory.path));

    const nlohmann::json provenance = readProvenance(entry.pyramidPath);
    ASSERT_TRUE(provenance.is_object());
    EXPECT_EQ(provenance.value("formatVersion", 0), PYRAMID_FORMAT_VERSION);
    ASSERT_TRUE(provenance.contains("source"));
    EXPECT_TRUE(provenance["source"].is_object());
    EXPECT_TRUE(provenance["source"].contains("path"));
}

// Une provenance d'un autre format est invalide, même si la source n'a pas changé : ses pixels ne peuvent pas être
// relus tels quels. C'est le cas des caches écrits avant la version 2, qui gardaient l'empreinte à la racine du JSON.
TEST(PyramidCacheTest, ProvenanceOfAnotherFormatVersionIsInvalid) {
    const TestDirectory directory;
    const fs::path source = directory.path / "a.tif";
    writeGreyImage(source, IMAGE_SIZE);
    const fs::path cache = IONs::pyramidCacheDirectory(directory.path);
    const IONs::PyramidCacheEntry entry = prepare(source, cache);

    {   // provenance à plat, comme avant la version 2 : l'empreinte de la source sans l'envelopper
        std::ofstream stream(provenanceOf(entry.pyramidPath), std::ios::binary);
        stream << IONs::computeFileFingerprint(source).dump();
    }
    EXPECT_FALSE(IONs::isPyramidCacheValid(source, entry.pyramidPath));
    EXPECT_FALSE(prepare(source, cache).reused);   // réécrite, pas réutilisée

    {   // version inconnue, provenance bien formée par ailleurs
        nlohmann::json provenance = readProvenance(entry.pyramidPath);
        provenance["formatVersion"] = PYRAMID_FORMAT_VERSION + 1;
        std::ofstream stream(provenanceOf(entry.pyramidPath), std::ios::binary);
        stream << provenance.dump();
    }
    EXPECT_FALSE(IONs::isPyramidCacheValid(source, entry.pyramidPath));
}

// Une pyramide dont la source a disparu est supprimée avec sa provenance ; celle dont la source est là ne bouge pas.
TEST(PyramidCacheTest, OrphanPyramidIsRemovedAndTheOthersAreKept) {
    const TestDirectory directory;
    const fs::path kept = directory.path / "garde.tif";
    const fs::path gone = directory.path / "parti.tif";
    writeGreyImage(kept, IMAGE_SIZE);
    writeGreyImage(gone, IMAGE_SIZE);
    const fs::path cache = IONs::pyramidCacheDirectory(directory.path);
    const IONs::PyramidCacheEntry keptEntry = prepare(kept, cache);
    const IONs::PyramidCacheEntry goneEntry = prepare(gone, cache);

    ASSERT_TRUE(fs::remove(gone));
    EXPECT_EQ(IONs::removeOrphanPyramids(cache), 2U);   // la pyramide et sa provenance
    EXPECT_FALSE(fs::exists(goneEntry.pyramidPath));
    EXPECT_TRUE(fs::exists(keptEntry.pyramidPath));
    EXPECT_TRUE(IONs::isPyramidCacheValid(kept, keptEntry.pyramidPath));
}

// Un temporaire de publication (…_pyramid.tif.<horloge>.<n>.tmp) appartient à une écriture en cours, peut-être dans un
// autre processus : le ménage ne doit jamais y toucher, même quand il vide tout.
TEST(PyramidCacheTest, TemporaryFileOfAPublicationIsNeverRemoved) {
    const TestDirectory directory;
    const fs::path source = directory.path / "a.tif";
    writeGreyImage(source, IMAGE_SIZE);
    const fs::path cache = IONs::pyramidCacheDirectory(directory.path);
    const IONs::PyramidCacheEntry entry = prepare(source, cache);

    fs::path temporary = entry.pyramidPath;
    temporary += ".1234.0.tmp";
    {
        std::ofstream stream(temporary, std::ios::binary);
        stream << "ecriture en cours";
    }

    EXPECT_EQ(IONs::removeAllPyramids(cache), 2U);
    EXPECT_FALSE(fs::exists(entry.pyramidPath));
    EXPECT_TRUE(fs::exists(temporary));
}

// Le ménage sur l'âge regarde le dernier emploi, pas la date du fichier.
TEST(PyramidCacheTest, StalePyramidIsRemovedAndZeroDisablesTheSweep) {
    const TestDirectory directory;
    const fs::path source = directory.path / "a.tif";
    writeGreyImage(source, IMAGE_SIZE);
    const fs::path cache = IONs::pyramidCacheDirectory(directory.path);
    const IONs::PyramidCacheEntry entry = prepare(source, cache);
    setLastReadDaysAgo(entry.pyramidPath, 20);

    EXPECT_EQ(IONs::removeStalePyramids(cache, 0), 0U);    // désactivé
    EXPECT_EQ(IONs::removeStalePyramids(cache, -1), 0U);   // désactivé aussi
    EXPECT_EQ(IONs::removeStalePyramids(cache, 30), 0U);   // pas encore périmée
    EXPECT_TRUE(fs::exists(entry.pyramidPath));

    EXPECT_EQ(IONs::removeStalePyramids(cache, 15), 2U);
    EXPECT_FALSE(fs::exists(entry.pyramidPath));
}

// Réemployer une pyramide rafraîchit sa date : c'est ce qui empêche le ménage de supprimer un cache qui sert tous les
// jours. Et écrire cette date ne doit surtout pas invalider le cache qu'on vient de valider.
TEST(PyramidCacheTest, ReusingAPyramidRefreshesLastReadWithoutInvalidatingIt) {
    const TestDirectory directory;
    const fs::path source = directory.path / "a.tif";
    writeGreyImage(source, IMAGE_SIZE);
    const fs::path cache = IONs::pyramidCacheDirectory(directory.path);
    const IONs::PyramidCacheEntry created = prepare(source, cache);
    setLastReadDaysAgo(created.pyramidPath, 20);

    EXPECT_TRUE(prepare(source, cache).reused);
    EXPECT_TRUE(IONs::isPyramidCacheValid(source, created.pyramidPath));
    EXPECT_EQ(IONs::removeStalePyramids(cache, 15), 0U);   // plus périmée : elle vient de servir
    EXPECT_TRUE(fs::exists(created.pyramidPath));
}

// Un cache qu'on n'arrive pas à inspecter ne doit pas faire échouer l'import : aucune des trois ne lève.
TEST(PyramidCacheTest, SweepingAMissingDirectoryIsNotAnError) {
    const TestDirectory directory;
    const fs::path cache = directory.path / "aucun-cache";
    EXPECT_EQ(IONs::removeOrphanPyramids(cache), 0U);
    EXPECT_EQ(IONs::removeStalePyramids(cache, 15), 0U);
    EXPECT_EQ(IONs::removeAllPyramids(cache), 0U);
}

// Un même fichier atteint de plusieurs façons (détour par « .. », séparateurs, casse du lecteur sous Windows) doit
// donner UNE seule clé de cache : sinon on écrit deux pyramides complètes pour les mêmes pixels.
TEST(PyramidCacheTest, TheSameFileReachedTwoWaysHasOneCacheKey) {
    const TestDirectory directory;
    const fs::path source = directory.path / "a.tif";
    writeGreyImage(source, IMAGE_SIZE);
    ASSERT_TRUE(fs::create_directories(directory.path / "detour"));
    const fs::path cache = IONs::pyramidCacheDirectory(directory.path);

    const fs::path throughParent = directory.path / "detour" / ".." / "a.tif";
    EXPECT_EQ(IONs::pyramidCachePath(cache, throughParent), IONs::pyramidCachePath(cache, source));

    // Le cache écrit par un chemin est réutilisé par l'autre, sans relire l'image.
    const IONs::PyramidCacheEntry created = prepare(source, cache);
    const IONs::PyramidCacheEntry reused = prepare(throughParent, cache);
    EXPECT_TRUE(reused.reused);
    EXPECT_EQ(reused.pyramidPath, created.pyramidPath);
    EXPECT_TRUE(IONs::isPyramidCacheValid(throughParent, created.pyramidPath));
}

// Une pyramide dont le nom ne correspond plus à la clé de sa propre source est inatteignable : personne ne la
// retrouvera, même si l'image est toujours là. C'est le cas d'un cache écrit avant un changement de règle de nommage,
// ou d'un dossier d'œuvre déplacé.
TEST(PyramidCacheTest, PyramidWithAnOutdatedKeyIsRemoved) {
    const TestDirectory directory;
    const fs::path source = directory.path / "a.tif";
    writeGreyImage(source, IMAGE_SIZE);
    const fs::path cache = IONs::pyramidCacheDirectory(directory.path);
    const IONs::PyramidCacheEntry entry = prepare(source, cache);

    // Même fichier, même provenance valide, mais rangé sous un autre nom que celui de sa clé.
    fs::path misnamed = cache / "a_00000000_pyramid.tif";
    ASSERT_TRUE(fs::copy_file(entry.pyramidPath, misnamed));
    fs::path misnamedProvenance = misnamed;
    misnamedProvenance += ".source.json";
    ASSERT_TRUE(fs::copy_file(provenanceOf(entry.pyramidPath), misnamedProvenance));

    EXPECT_EQ(IONs::removeOrphanPyramids(cache), 2U);
    EXPECT_FALSE(fs::exists(misnamed));
    EXPECT_TRUE(fs::exists(entry.pyramidPath));   // celle qui porte la bonne clé reste
}
