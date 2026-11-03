// tests/core/test_pyramidal_tiff_reader.cpp
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/pyramidal_tiff_writer.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>

#include <filesystem>
#include <string>

namespace {

    TEST(PyramidalTiffReader, ReadsRegionAcrossTilesAndReducedLevel) {
        // Crée une image assez grande pour que la région traverse plusieurs tuiles TIFF.
        const auto path = std::filesystem::temp_directory_path() / "kikirpa_pyramidal_reader_test.tif";
        cv::Mat source(700, 900, CV_16UC3);
        // Chaque pixel contient ses coordonnées : cela permet de vérifier précisément la copie.
        for (int y = 0; y < source.rows; ++y) {
            for (int x = 0; x < source.cols; ++x)
                source.at<cv::Vec<uint16_t, 3>>(y, x) = {static_cast<uint16_t>(x), static_cast<uint16_t>(y), 123};
        }

        // Écrit la pyramide puis crée le lecteur associé au fichier.
        std::string error;
        ASSERT_TRUE(IONs::createPyramidalTiledTiff(source, path, error)) << error;
        IONs::PyramidalTiffReader reader(path);

        // Lit une région du niveau original, en traversant plusieurs tuiles.
        cv::Mat region;
        ASSERT_TRUE(reader.readRegion(0, cv::Rect(500, 300, 250, 250), region, error)) << error;
        // Vérifie la taille, le type et les pixels aux deux coins de la région.
        ASSERT_EQ(region.type(), source.type());
        ASSERT_EQ(region.size(), cv::Size(250, 250));
        const cv::Vec<uint16_t, 3> firstPixel{500, 300, 123};
        const cv::Vec<uint16_t, 3> lastPixel{749, 549, 123};
        const auto actualFirstPixel = region.at<cv::Vec<uint16_t, 3>>(0, 0);
        const auto actualLastPixel = region.at<cv::Vec<uint16_t, 3>>(249, 249);
        EXPECT_EQ(actualFirstPixel, firstPixel);
        EXPECT_EQ(actualLastPixel, lastPixel);

        // Vérifie aussi qu'une région peut être lue dans un niveau réduit.
        cv::Mat reduced;
        ASSERT_TRUE(reader.readRegion(1, cv::Rect(0, 0, 450, 350), reduced, error)) << error;
        EXPECT_EQ(reduced.size(), cv::Size(450, 350));

        // Supprime le fichier temporaire créé pour le test.
        std::filesystem::remove(path);
    }

    TEST(PyramidalTiffReader, RejectsRegionOutsideLevel) {
        // Crée une petite pyramide pour tester le contrôle des limites.
        const auto path = std::filesystem::temp_directory_path() / "kikirpa_pyramidal_reader_bounds_test.tif";
        cv::Mat source = cv::Mat::zeros(32, 32, CV_8UC1);
        std::string error;
        ASSERT_TRUE(IONs::createPyramidalTiledTiff(source, path, error)) << error;

        // Demande une région qui dépasse l'image : la lecture doit échouer.
        IONs::PyramidalTiffReader reader(path);
        cv::Mat output;
        EXPECT_FALSE(reader.readRegion(0, cv::Rect(31, 31, 2, 2), output, error));
        EXPECT_FALSE(error.empty());

        // Supprime le fichier temporaire même après le test de l'erreur.
        std::filesystem::remove(path);
    }

}
