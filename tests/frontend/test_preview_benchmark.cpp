// tests/frontend/test_preview_benchmark.cpp
#include "frontendutils.h"
#include "core/io/pyramidal_tiff_writer.h"
#include "core/io/unicode_path.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <QElapsedTimer>
#include <QImageReader>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr int BENCHMARK_WIDTH = 2400;
constexpr int BENCHMARK_HEIGHT = 1800;
constexpr int ITERATIONS = 5;
const QSize PREVIEW_SIZE(400, 300);

TEST(PreviewBenchmark, CompareRawTiffVsPyramidalTiff) {
    const fs::path tempDir = fs::temp_directory_path() / "kikirpa_preview_benchmark";
    fs::create_directories(tempDir);
    const fs::path rawTiffPath = tempDir / "bench_raw.tif";
    const fs::path pyramidDir = tempDir / ".cache" / "pyramids";
    fs::create_directories(pyramidDir);
    const fs::path pyramidTiffPath = pyramidDir / "0_bench_raw_pyramid.tif";

    // 1. Génère une image 16 bits de test réaliste
    cv::Mat testImage(BENCHMARK_HEIGHT, BENCHMARK_WIDTH, CV_16UC3);
    for (int y = 0; y < testImage.rows; ++y) {
        for (int x = 0; x < testImage.cols; ++x) {
            testImage.at<cv::Vec3w>(y, x) = cv::Vec3w(
                static_cast<uint16_t>((x * 37) % 65535),
                static_cast<uint16_t>((y * 43) % 65535),
                static_cast<uint16_t>(((x + y) * 29) % 65535)
            );
        }
    }

    ASSERT_TRUE(IONs::writeImage(rawTiffPath, testImage));

    std::string errorMessage;
    ASSERT_TRUE(IONs::createPyramidalTiledTiff(testImage, pyramidTiffPath, errorMessage)) << errorMessage;

    // 2. Mesure : Ancienne méthode (QImageReader direct sur TIFF brut, sans pyramide)
    const QString rawPathQt = QString::fromStdString(IONs::pathToUtf8(rawTiffPath));
    qint64 totalRawTimeMs = 0;
    QImage rawPreview;

    for (int i = 0; i < ITERATIONS; ++i) {
        QElapsedTimer timer;
        timer.start();
        QImageReader reader(rawPathQt);
        reader.setAutoTransform(true);
        rawPreview = reader.read();
        if (!rawPreview.isNull()) {
            rawPreview = rawPreview.scaled(PREVIEW_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        totalRawTimeMs += timer.elapsed();
    }
    const double avgRawMs = static_cast<double>(totalRawTimeMs) / ITERATIONS;

    // 3. Mesure : Nouvelle méthode (Lecture via TIFF pyramidal)
    const QString pyramidPathQt = QString::fromStdString(IONs::pathToUtf8(pyramidTiffPath));
    qint64 totalPyramidTimeMs = 0;
    QImage pyramidPreview;

    for (int i = 0; i < ITERATIONS; ++i) {
        QElapsedTimer timer;
        timer.start();
        pyramidPreview = FrontendUtils::readPyramidalPreview(pyramidPathQt, PREVIEW_SIZE);
        totalPyramidTimeMs += timer.elapsed();
    }
    const double avgPyramidMs = static_cast<double>(totalPyramidTimeMs) / ITERATIONS;

    // 4. Vérification que les deux vignettes sont valides
    EXPECT_FALSE(rawPreview.isNull());
    EXPECT_FALSE(pyramidPreview.isNull());

    const double speedup = avgPyramidMs > 0 ? (avgRawMs / avgPyramidMs) : 1.0;

    std::cout << "\n===================================================================\n";
    std::cout << "           BENCHMARK DE RAPIDITE D'AFFICHAGE DES VIGNETTES          \n";
    std::cout << "  Image source : " << BENCHMARK_WIDTH << "x" << BENCHMARK_HEIGHT << " (16-bit RGB)\n";
    std::cout << "  Taille vignette cible : " << PREVIEW_SIZE.width() << "x" << PREVIEW_SIZE.height() << "\n";
    std::cout << "-------------------------------------------------------------------\n";
    std::cout << "  Ancienne méthode (QImageReader brut) : " << avgRawMs << " ms / image\n";
    std::cout << "  Nouvelle méthode (TIFF Pyramidal)    : " << avgPyramidMs << " ms / image\n";
    std::cout << "  --> ACCELERATION : " << speedup << "x plus rapide !\n";
    std::cout << "===================================================================\n\n";

    // Nettoyage des fichiers temporaires
    std::error_code ec;
    fs::remove_all(tempDir, ec);
}

}
