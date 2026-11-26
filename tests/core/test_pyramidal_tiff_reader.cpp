// tests/core/test_pyramidal_tiff_reader.cpp
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

#include <vector>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

    constexpr int SOURCE_HEIGHT = 700;
    constexpr int SOURCE_WIDTH = 900;
    constexpr uint16_t BLUE_CHANNEL = 123;
    constexpr int REGION_X = 500;
    constexpr int REGION_Y = 300;
    constexpr int REGION_WIDTH = 250;
    constexpr int REGION_HEIGHT = 250;
    constexpr int REDUCED_WIDTH = 450;
    constexpr int REDUCED_HEIGHT = 350;
    constexpr int BOUNDS_IMAGE_SIZE = 32;
    constexpr int OUTSIDE_REGION_X = 31;
    constexpr int OUTSIDE_REGION_Y = 31;
    constexpr int OUTSIDE_REGION_SIZE = 2;

    TEST(PyramidalTiffReaderClass, ReadsRegionAcrossTilesAndReducedLevel) {
        // Crée une image assez grande pour que la région traverse plusieurs tuiles TIFF.
        const auto PATH = std::filesystem::temp_directory_path() / "kikirpa_pyramidal_reader_test.tif";
        cv::Mat source(SOURCE_HEIGHT, SOURCE_WIDTH, CV_16UC3);
        // Chaque pixel contient ses coordonnées : cela permet de vérifier précisément la copie.
        for (int y = 0; y < source.rows; ++y) {
            for (int x = 0; x < source.cols; ++x)
                source.at<cv::Vec<uint16_t, 3>>(y, x) = {
                    static_cast<uint16_t>(x), static_cast<uint16_t>(y), BLUE_CHANNEL};
        }

        // Écrit la pyramide puis crée le lecteur associé au fichier.
        std::string error;
        ASSERT_TRUE(IONs::createPyramidalTiledTiff(source, PATH, error)) << error;
        IONs::PyramidalTiffReaderClass reader(PATH);

        // Lit une région du niveau original, en traversant plusieurs tuiles.
        cv::Mat region;
        ASSERT_TRUE(reader.readRegion(
            0, cv::Rect(REGION_X, REGION_Y, REGION_WIDTH, REGION_HEIGHT), region, error)) << error;
        // Vérifie la taille, le type et les pixels aux deux coins de la région.
        ASSERT_EQ(region.type(), source.type());
        ASSERT_EQ(region.size(), cv::Size(REGION_WIDTH, REGION_HEIGHT));
        const cv::Vec<uint16_t, 3> FIRST_PIXEL{REGION_X, REGION_Y, BLUE_CHANNEL};
        const cv::Vec<uint16_t, 3> LAST_PIXEL{
            REGION_X + REGION_WIDTH - 1, REGION_Y + REGION_HEIGHT - 1, BLUE_CHANNEL};
        const auto ACTUAL_FIRST_PIXEL = region.at<cv::Vec<uint16_t, 3>>(0, 0);
        const auto ACTUAL_LAST_PIXEL = region.at<cv::Vec<uint16_t, 3>>(
            REGION_HEIGHT - 1, REGION_WIDTH - 1);
        EXPECT_EQ(ACTUAL_FIRST_PIXEL, FIRST_PIXEL);
        EXPECT_EQ(ACTUAL_LAST_PIXEL, LAST_PIXEL);

        // Vérifie aussi qu'une région peut être lue dans un niveau réduit.
        cv::Mat reduced;
        ASSERT_TRUE(reader.readRegion(
            1, cv::Rect(0, 0, REDUCED_WIDTH, REDUCED_HEIGHT), reduced, error)) << error;
        EXPECT_EQ(reduced.size(), cv::Size(REDUCED_WIDTH, REDUCED_HEIGHT));

        // Vérifie getLevelCount, getLevelDimensions et readLevel
        EXPECT_EQ(reader.getLevelCount(), 4);
        int w0 = 0;
        int h0 = 0;
        ASSERT_TRUE(reader.getLevelDimensions(0, w0, h0, error)) << error;
        EXPECT_EQ(w0, SOURCE_WIDTH);
        EXPECT_EQ(h0, SOURCE_HEIGHT);

        int w1 = 0;
        int h1 = 0;
        ASSERT_TRUE(reader.getLevelDimensions(1, w1, h1, error)) << error;
        EXPECT_EQ(w1, REDUCED_WIDTH);
        EXPECT_EQ(h1, REDUCED_HEIGHT);

        cv::Mat fullLevel1;
        ASSERT_TRUE(reader.readLevel(1, fullLevel1, error)) << error;
        EXPECT_EQ(fullLevel1.size(), cv::Size(REDUCED_WIDTH, REDUCED_HEIGHT));
        EXPECT_EQ(fullLevel1.type(), source.type());

        // Supprime le fichier temporaire créé pour le test.
        std::filesystem::remove(PATH);
    }

    // Image à 4 canaux (RGBA) : le canal alpha est déclaré par TIFFTAG_EXTRASAMPLES. Passer la valeur au lieu d'un
    // tableau faisait planter libtiff.
    TEST(PyramidalTiffReaderClass, WritesAndReadsFourChannelImage) {
        const auto PATH = std::filesystem::temp_directory_path() / "kikirpa_pyramidal_reader_rgba_test.tif";
        const cv::Mat source(BOUNDS_IMAGE_SIZE, BOUNDS_IMAGE_SIZE, CV_8UC4, cv::Scalar(10, 20, 30, 255));
        std::string error;
        ASSERT_TRUE(IONs::createPyramidalTiledTiff(source, PATH, error)) << error;

        cv::Mat region;
        ASSERT_TRUE(IONs::PyramidalTiffReaderClass(PATH).readRegion(
            0, cv::Rect(0, 0, BOUNDS_IMAGE_SIZE, BOUNDS_IMAGE_SIZE), region, error)) << error;
        ASSERT_EQ(region.type(), CV_8UC4);
        EXPECT_EQ(region.at<cv::Vec4b>(0, 0), cv::Vec4b(10, 20, 30, 255));
        std::filesystem::remove(PATH);
    }

    TEST(PyramidalTiffReaderClass, RejectsRegionOutsideLevel) {
        // Crée une petite pyramide pour tester le contrôle des limites.
        const auto PATH = std::filesystem::temp_directory_path() / "kikirpa_pyramidal_reader_bounds_test.tif";
        cv::Mat source = cv::Mat::zeros(BOUNDS_IMAGE_SIZE, BOUNDS_IMAGE_SIZE, CV_8UC1);
        std::string error;
        ASSERT_TRUE(IONs::createPyramidalTiledTiff(source, PATH, error)) << error;

        // Demande une région qui dépasse l'image : la lecture doit échouer.
        IONs::PyramidalTiffReaderClass reader(PATH);
        cv::Mat output;
        EXPECT_FALSE(reader.readRegion(
            0,
            cv::Rect(OUTSIDE_REGION_X, OUTSIDE_REGION_Y, OUTSIDE_REGION_SIZE, OUTSIDE_REGION_SIZE),
            output,
            error
        ));
        EXPECT_FALSE(error.empty());

        // Supprime le fichier temporaire même après le test de l'erreur.
        std::filesystem::remove(PATH);
    }

    TEST(PyramidalTiffReaderClass, ReadsMetadataSuccessfully) {
        // Crée une image basique pour le test
        const auto PATH = std::filesystem::temp_directory_path() / "kikirpa_pyramidal_reader_metadata_test.tif";
        cv::Mat source = cv::Mat::zeros(BOUNDS_IMAGE_SIZE, BOUNDS_IMAGE_SIZE, CV_8UC1);
        std::string error;
        
        // Génération du TIFF
        ASSERT_TRUE(IONs::createPyramidalTiledTiff(source, PATH, error)) << error;

        // Instanciation du lecteur
        IONs::PyramidalTiffReaderClass reader(PATH);
        
        std::vector<uint8_t> iccProfile;
        double scaleX = 0.0;
        double scaleY = 0.0;

        // Exécution de la fonction
        ASSERT_TRUE(reader.readMetadata(iccProfile, scaleX, scaleY, error)) << error;
        
        // Vérifications
        EXPECT_TRUE(error.empty());
        EXPECT_GT(scaleX, 0.0); // L'échelle doit être strictement positive (généralement 1.0 si non spécifiée)
        EXPECT_GT(scaleY, 0.0);
        // Selon comment est codé createPyramidalTiledTiff, iccProfile sera soit vide, soit contenant des octets.
        // L'important est que la fonction n'ait pas échoué.
        
        // Nettoyage
        std::filesystem::remove(PATH);
    }

    TEST(PyramidalTiffReaderClass, FailsOnMissingFile) {
        // On donne un chemin vers un fichier qui n'existe pas
        const auto PATH = std::filesystem::temp_directory_path() / "kikirpa_non_existent_file.tif";
        IONs::PyramidalTiffReaderClass reader(PATH);
        
        cv::Mat output;
        std::string error;
        
        // La lecture doit échouer et renvoyer false
        EXPECT_FALSE(reader.readRegion(0, cv::Rect(0, 0, 100, 100), output, error));
        // Un message d'erreur clair doit être généré
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(output.empty(), true); // La matrice de sortie doit rester vide
    }

    TEST(PyramidalTiffReaderClass, RejectsInvalidArguments) {
        // Setup : Création d'une image basique et valide
        const auto PATH = std::filesystem::temp_directory_path() / "kikirpa_invalid_args_test.tif";
        cv::Mat source = cv::Mat::zeros(BOUNDS_IMAGE_SIZE, BOUNDS_IMAGE_SIZE, CV_8UC1);
        std::string error;
        ASSERT_TRUE(IONs::createPyramidalTiledTiff(source, PATH, error)) << error;

        IONs::PyramidalTiffReaderClass reader(PATH);
        cv::Mat output;

        // Cas 1 : Niveau de pyramide négatif
        EXPECT_FALSE(reader.readRegion(-1, cv::Rect(0, 0, 10, 10), output, error));
        EXPECT_FALSE(error.empty());

        // Cas 2 : Coordonnée X négative
        EXPECT_FALSE(reader.readRegion(0, cv::Rect(-5, 0, 10, 10), output, error));
        EXPECT_FALSE(error.empty());

        // Cas 3 : Largeur nulle (ou négative)
        EXPECT_FALSE(reader.readRegion(0, cv::Rect(0, 0, 0, 10), output, error));
        EXPECT_FALSE(error.empty());

        // Nettoyage
        std::filesystem::remove(PATH);
    }

    TEST(PyramidalTiffWriterClass, WriteFailureCleansUpWithoutLeavingCorruptedOrTemporaryFile) {
        cv::Mat validImage = cv::Mat::zeros(64, 64, CV_8UC3);
        std::string errorMessage;

        // Cas 1 : Image vide -> échec immédiat, aucun fichier créé
        const auto nonExistentPath = std::filesystem::temp_directory_path() / "kikirpa_should_never_exist.tif";
        std::filesystem::remove(nonExistentPath);
        cv::Mat emptyImage;
        EXPECT_FALSE(IONs::createPyramidalTiledTiff(emptyImage, nonExistentPath, errorMessage));
        EXPECT_FALSE(errorMessage.empty());
        EXPECT_FALSE(std::filesystem::exists(nonExistentPath));

        // Cas 2 : Dossier parent impossible (un fichier régulier bloque la création de dossier)
        const auto blockingFile = std::filesystem::temp_directory_path() / "kikirpa_blocking_file.txt";
        {
            std::ofstream out(blockingFile);
            out << "block";
        }
        const auto impossibleDest = blockingFile / "impossible_subfolder" / "pyramid.tif";
        EXPECT_FALSE(IONs::createPyramidalTiledTiff(validImage, impossibleDest, errorMessage));
        EXPECT_FALSE(errorMessage.empty());
        EXPECT_FALSE(std::filesystem::exists(impossibleDest));

        std::filesystem::remove(blockingFile);
    }


    // ---------------------------------------------------------------------------------------------------------------
    // Sources à BANDES : l'organisation des TIFF d'acquisition. Le cache, lui, est toujours tuilé. Le lecteur doit
    // savoir lire une région des deux, pour que le niveau 0 puisse venir de l'image de l'œuvre au lieu du cache.

    constexpr int STRIP_SOURCE_WIDTH = 300;
    constexpr int STRIP_SOURCE_HEIGHT = 200;
    constexpr int STRIP_REGION_X = 37;
    constexpr int STRIP_REGION_Y = 11;
    constexpr int STRIP_REGION_WIDTH = 150;
    constexpr int STRIP_REGION_HEIGHT = 93;
    constexpr uint32_t SHORT_STRIP_ROWS = 7;

    cv::Mat makeCoordinateImage(int width, int height) {
        cv::Mat image(height, width, CV_16UC3);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                image.at<cv::Vec<uint16_t, 3>>(y, x) = {static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                                                        static_cast<uint16_t>(x + y)};
        return image;
    }

    // Écrit une image à bandes, avec la compression et la hauteur de bande demandées.
    void writeStripTiff(const std::filesystem::path& path, const cv::Mat& image, uint16_t compression,
                        uint32_t rowsPerStrip) {
        TIFF* file = IONs::openTiff(path, "w");
        ASSERT_NE(file, nullptr);
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(image.cols)));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(image.rows)));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(image.channels())));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(16)));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_COMPRESSION, compression));
        ASSERT_TRUE(TIFFSetField(file, TIFFTAG_ROWSPERSTRIP, rowsPerStrip));
        for (int row = 0; row < image.rows; ++row) {
            void* rowData = const_cast<void*>(static_cast<const void*>(image.ptr(row)));
            ASSERT_NE(TIFFWriteScanline(file, rowData, static_cast<uint32_t>(row), 0), -1);
        }
        TIFFClose(file);
    }

    void expectReadsTheSameRegion(const std::filesystem::path& path, const cv::Mat& source) {
        const cv::Rect region(STRIP_REGION_X, STRIP_REGION_Y, STRIP_REGION_WIDTH, STRIP_REGION_HEIGHT);
        const IONs::PyramidalTiffReaderClass reader(path);
        cv::Mat read;
        std::string error;
        ASSERT_TRUE(reader.readRegion(0, region, read, error)) << error;
        ASSERT_EQ(read.size(), region.size());
        ASSERT_EQ(read.type(), source.type());
        EXPECT_EQ(cv::norm(read, source(region), cv::NORM_INF), 0.0);
    }

    // Non compressée, une seule bande de 200 lignes : c'est 02-Philadelphia (une bande de 8 700 lignes). La lecture
    // passe par les décalages, sans décoder l'image entière.
    TEST(PyramidalTiffReaderClass, ReadsARegionOfAnUncompressedSingleStripSource) {
        const auto path = std::filesystem::temp_directory_path() / "kikirpa_reader_uncompressed_strip.tif";
        const cv::Mat source = makeCoordinateImage(STRIP_SOURCE_WIDTH, STRIP_SOURCE_HEIGHT);
        writeStripTiff(path, source, COMPRESSION_NONE, static_cast<uint32_t>(STRIP_SOURCE_HEIGHT));
        expectReadsTheSameRegion(path, source);
        std::filesystem::remove(path);
    }

    // Compressée en bandes courtes : c'est 01-Nominal (LZW, 25 lignes par bande). libtiff décode les bandes
    // recoupées, et la région commence volontairement au milieu de l'une d'elles.
    TEST(PyramidalTiffReaderClass, ReadsARegionOfACompressedShortStripSource) {
        const auto path = std::filesystem::temp_directory_path() / "kikirpa_reader_compressed_strips.tif";
        const cv::Mat source = makeCoordinateImage(STRIP_SOURCE_WIDTH, STRIP_SOURCE_HEIGHT);
        writeStripTiff(path, source, COMPRESSION_ADOBE_DEFLATE, SHORT_STRIP_ROWS);
        expectReadsTheSameRegion(path, source);
        std::filesystem::remove(path);
    }

    // Une image à bandes n'a qu'un niveau : le lecteur ne doit pas prétendre le contraire.
    TEST(PyramidalTiffReaderClass, AStripSourceHasASingleLevel) {
        const auto path = std::filesystem::temp_directory_path() / "kikirpa_reader_strip_levels.tif";
        const cv::Mat source = makeCoordinateImage(STRIP_SOURCE_WIDTH, STRIP_SOURCE_HEIGHT);
        writeStripTiff(path, source, COMPRESSION_NONE, SHORT_STRIP_ROWS);

        const IONs::PyramidalTiffReaderClass reader(path);
        EXPECT_EQ(reader.getLevelCount(), 1);
        int width = 0;
        int height = 0;
        std::string error;
        ASSERT_TRUE(reader.getLevelDimensions(0, width, height, error)) << error;
        EXPECT_EQ(width, STRIP_SOURCE_WIDTH);
        EXPECT_EQ(height, STRIP_SOURCE_HEIGHT);
        cv::Mat unused;
        EXPECT_FALSE(reader.readRegion(1, cv::Rect(0, 0, 1, 1), unused, error));
        std::filesystem::remove(path);
    }

}
