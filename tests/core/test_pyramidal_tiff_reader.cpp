// tests/core/test_pyramidal_tiff_reader.cpp
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/pyramidal_tiff_writer.h"

#include <gtest/gtest.h>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/types.hpp>

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

        // Supprime le fichier temporaire créé pour le test.
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

}
