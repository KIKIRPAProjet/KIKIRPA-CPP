// tests/core/test_pyramidal_tiff_reader.cpp
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/pyramidal_tiff_writer.h"

#include <gtest/gtest.h>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/types.hpp>

#include <vector>
#include <cstdint>
#include <filesystem>
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
        const auto path = std::filesystem::temp_directory_path() / "kikirpa_pyramidal_reader_test.tif";
        cv::Mat source(SOURCE_HEIGHT, SOURCE_WIDTH, CV_16UC3);
        // Chaque pixel contient ses coordonnées : cela permet de vérifier précisément la copie.
        for (int y = 0; y < source.rows; ++y) {
            for (int x = 0; x < source.cols; ++x)
                source.at<cv::Vec<uint16_t, 3>>(y, x) = {
                    static_cast<uint16_t>(x), static_cast<uint16_t>(y), BLUE_CHANNEL};
        }

        // Écrit la pyramide puis crée le lecteur associé au fichier.
        std::string error;
        ASSERT_TRUE(IONs::createPyramidalTiledTiff(source, path, error)) << error;
        IONs::PyramidalTiffReaderClass reader(path);

        std::vector<cv::Size> pyramidDimensions;
        ASSERT_TRUE(reader.readPyramidDimensions(pyramidDimensions, error)) << error;
        ASSERT_EQ(pyramidDimensions.size(), 4U);
        EXPECT_EQ(pyramidDimensions.at(0), cv::Size(SOURCE_WIDTH, SOURCE_HEIGHT));
        EXPECT_EQ(pyramidDimensions.at(1), cv::Size(REDUCED_WIDTH, REDUCED_HEIGHT));

        IONs::PyramidViewportRegion viewport;
        ASSERT_TRUE(reader.readViewport(
            IONs::PyramidViewportRequest{cv::Point2d(100, 200), cv::Size(120, 80), 100.0},
            viewport, error)) << error;
        EXPECT_EQ(viewport.level, 0);
        EXPECT_EQ(viewport.region, cv::Rect(100, 200, 120, 80));
        EXPECT_EQ(viewport.fullResolutionRegion, cv::Rect2d(100, 200, 120, 80));
        const cv::Vec<uint16_t, 3> viewportFirstPixel{100, 200, BLUE_CHANNEL};
        const cv::Vec<uint16_t, 3> actualViewportFirstPixel =
            viewport.pixels.at<cv::Vec<uint16_t, 3>>(0, 0);
        EXPECT_EQ(actualViewportFirstPixel, viewportFirstPixel);

        ASSERT_TRUE(reader.readViewport(
            IONs::PyramidViewportRequest{cv::Point2d(40, 20), cv::Size(100, 75), 25.0},
            viewport, error)) << error;
        EXPECT_EQ(viewport.level, 2);
        EXPECT_EQ(viewport.region, cv::Rect(10, 5, 100, 75));
        EXPECT_EQ(viewport.fullResolutionRegion, cv::Rect2d(40, 20, 400, 300));

        ASSERT_TRUE(reader.readViewport(
            IONs::PyramidViewportRequest{cv::Point2d(850, 650), cv::Size(100, 100), 100.0},
            viewport, error)) << error;
        EXPECT_EQ(viewport.region, cv::Rect(850, 650, 50, 50));
        EXPECT_EQ(viewport.fullResolutionRegion, cv::Rect2d(850, 650, 50, 50));

        ASSERT_TRUE(reader.readViewport(
            IONs::PyramidViewportRequest{cv::Point2d(-20, -10), cv::Size(100, 100), 100.0},
            viewport, error)) << error;
        EXPECT_EQ(viewport.region, cv::Rect(0, 0, 80, 90));

        EXPECT_FALSE(reader.readViewport(
            IONs::PyramidViewportRequest{cv::Point2d(0, 0), cv::Size(100, 100), 0.0},
            viewport, error));
        EXPECT_FALSE(error.empty());

        // Lit une région du niveau original, en traversant plusieurs tuiles.
        cv::Mat region;
        ASSERT_TRUE(reader.readRegion(
            0, cv::Rect(REGION_X, REGION_Y, REGION_WIDTH, REGION_HEIGHT), region, error)) << error;
        // Vérifie la taille, le type et les pixels aux deux coins de la région.
        ASSERT_EQ(region.type(), source.type());
        ASSERT_EQ(region.size(), cv::Size(REGION_WIDTH, REGION_HEIGHT));
        const cv::Vec<uint16_t, 3> firstPixel{REGION_X, REGION_Y, BLUE_CHANNEL};
        const cv::Vec<uint16_t, 3> lastPixel{
            REGION_X + REGION_WIDTH - 1, REGION_Y + REGION_HEIGHT - 1, BLUE_CHANNEL};
        const auto actualFirstPixel = region.at<cv::Vec<uint16_t, 3>>(0, 0);
        const auto actualLastPixel = region.at<cv::Vec<uint16_t, 3>>(
            REGION_HEIGHT - 1, REGION_WIDTH - 1);
        EXPECT_EQ(actualFirstPixel, firstPixel);
        EXPECT_EQ(actualLastPixel, lastPixel);

        // Vérifie aussi qu'une région peut être lue dans un niveau réduit.
        cv::Mat reduced;
        ASSERT_TRUE(reader.readRegion(
            1, cv::Rect(0, 0, REDUCED_WIDTH, REDUCED_HEIGHT), reduced, error)) << error;
        EXPECT_EQ(reduced.size(), cv::Size(REDUCED_WIDTH, REDUCED_HEIGHT));

        // Supprime le fichier temporaire créé pour le test.
        std::filesystem::remove(path);
    }

    TEST(PyramidalTiffReaderClass, RejectsRegionOutsideLevel) {
        // Crée une petite pyramide pour tester le contrôle des limites.
        const auto path = std::filesystem::temp_directory_path() / "kikirpa_pyramidal_reader_bounds_test.tif";
        cv::Mat source = cv::Mat::zeros(BOUNDS_IMAGE_SIZE, BOUNDS_IMAGE_SIZE, CV_8UC1);
        std::string error;
        ASSERT_TRUE(IONs::createPyramidalTiledTiff(source, path, error)) << error;

        // Demande une région qui dépasse l'image : la lecture doit échouer.
        IONs::PyramidalTiffReaderClass reader(path);
        cv::Mat output;
        EXPECT_FALSE(reader.readRegion(
            0,
            cv::Rect(OUTSIDE_REGION_X, OUTSIDE_REGION_Y, OUTSIDE_REGION_SIZE, OUTSIDE_REGION_SIZE),
            output,
            error
        ));
        EXPECT_FALSE(error.empty());

        // Supprime le fichier temporaire même après le test de l'erreur.
        std::filesystem::remove(path);
    }

    TEST(PyramidalTiffReaderClass, ReadsMetadataSuccessfully) {
        // Crée une image basique pour le test
        const auto path = std::filesystem::temp_directory_path() / "kikirpa_pyramidal_reader_metadata_test.tif";
        cv::Mat source = cv::Mat::zeros(BOUNDS_IMAGE_SIZE, BOUNDS_IMAGE_SIZE, CV_8UC1);
        std::string error;
        
        // Génération du TIFF
        ASSERT_TRUE(IONs::createPyramidalTiledTiff(source, path, error)) << error;

        // Instanciation du lecteur
        IONs::PyramidalTiffReaderClass reader(path);
        
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
        std::filesystem::remove(path);
    }

    TEST(PyramidalTiffReaderClass, FailsOnMissingFile) {
        // On donne un chemin vers un fichier qui n'existe pas
        const auto path = std::filesystem::temp_directory_path() / "kikirpa_non_existent_file.tif";
        IONs::PyramidalTiffReaderClass reader(path);
        
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
        const auto path = std::filesystem::temp_directory_path() / "kikirpa_invalid_args_test.tif";
        cv::Mat source = cv::Mat::zeros(BOUNDS_IMAGE_SIZE, BOUNDS_IMAGE_SIZE, CV_8UC1);
        std::string error;
        ASSERT_TRUE(IONs::createPyramidalTiledTiff(source, path, error)) << error;

        IONs::PyramidalTiffReaderClass reader(path);
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
        std::filesystem::remove(path);
    }


}
