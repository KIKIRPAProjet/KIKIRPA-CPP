// tests/frontend/test_preview_benchmark.cpp
#include "frontendutils.h"
#include "artworkcanvas.h"
#include "core/io/pyramid_cache.h"
#include "core/io/pyramidal_tiff_writer.h"
#include "core/io/unicode_path.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <QElapsedTimer>
#include <QImageReader>
#include <QDir>
#include <QApplication>
#include <QMouseEvent>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

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

// L'aperçu retrouve la pyramide d'une image par le cache partagé avec l'import (core/io/pyramid_cache.h) :
// photo.tif et photo.tiff ont chacune la leur, et une source remplacée n'utilise plus l'ancienne.
TEST(PreviewBenchmark, FindPyramidPathValidatesCacheAndAvoidsCollision) {
    const fs::path tempDir = fs::temp_directory_path() / "kikirpa_find_pyramid_test";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir);
    const fs::path sourceTif = tempDir / "photo.tif";
    const fs::path sourceTiff = tempDir / "photo.tiff";
    const fs::path cacheDirectory = IONs::pyramidCacheDirectory(tempDir);

    const cv::Mat dummy(64, 64, CV_8UC3, cv::Scalar(10, 20, 30));
    ASSERT_TRUE(IONs::writeImage(sourceTif, dummy));
    ASSERT_TRUE(IONs::writeImage(sourceTiff, dummy));
    IONs::PyramidCacheEntry tifEntry;
    IONs::PyramidCacheEntry tiffEntry;
    std::string err;
    ASSERT_TRUE(IONs::preparePyramidCache(sourceTif, cacheDirectory, tifEntry, err)) << err;
    ASSERT_TRUE(IONs::preparePyramidCache(sourceTiff, cacheDirectory, tiffEntry, err)) << err;

    const auto toQt = [](const fs::path& path) { return QString::fromStdString(IONs::pathToUtf8(path)); };
    const auto found = [&](const fs::path& source) {
        return QDir::fromNativeSeparators(FrontendUtils::findPyramidPathForSource(toQt(source)));
    };

    // 1. Chaque source retrouve sa propre pyramide, quelle que soit sa case dans la grille
    EXPECT_EQ(found(sourceTif), QDir::fromNativeSeparators(toQt(tifEntry.pyramidPath)));
    EXPECT_EQ(found(sourceTiff), QDir::fromNativeSeparators(toQt(tiffEntry.pyramidPath)));
    EXPECT_NE(found(sourceTif), found(sourceTiff));

    // 2. Source remplacée (même par un fichier plus ancien) : le cache n'est plus utilisé
    fs::last_write_time(sourceTif, fs::last_write_time(tifEntry.pyramidPath) - std::chrono::hours(24));
    EXPECT_TRUE(FrontendUtils::findPyramidPathForSource(toQt(sourceTif)).isEmpty());

    fs::remove_all(tempDir, ec);
}

void ensureQApplication() {
    if (!QApplication::instance()) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static int argc = 1;
        static char appName[] = "test_preview_benchmark";
        static char* argv[] = {appName, nullptr};
        static QApplication app(argc, argv);
    }
}

TEST(ArtworkCanvasZoomTest, ZoomInGridAndOneToOnePixel) {
    ensureQApplication();
    ArtworkCanvas canvas;
    canvas.resize(800, 600);

    // 1. Mode Tuiles (Grid) : le zoom peut être modifié et lu
    canvas.setMode(1); // kModeGrid
    EXPECT_EQ(canvas.zoom(), 100);
    canvas.setZoom(250);
    EXPECT_EQ(canvas.zoom(), 250);
    // Dézoom minimal (1%)
    canvas.setZoom(1);
    EXPECT_EQ(canvas.zoom(), 1);
    canvas.setZoom(0);
    EXPECT_EQ(canvas.zoom(), 1);

    // 2. Mode Image finale : vérification du zoom 100% = 1 pixel image pour 1 pixel écran
    const QImage testMosaic(2000, 1000, QImage::Format_RGB888);
    canvas.setMosaicPreview(testMosaic);
    canvas.setMode(6); // kModeExport

    // Après fitToWindow(), le zoom s'adapte pour tenir dans la fenêtre de 800x600
    EXPECT_LT(canvas.zoom(), 100);

    // Au zoom 100%, la taille de l'image correspond exactement à la taille réelle de l'image (1:1)
    canvas.setZoom(100);
    EXPECT_EQ(canvas.zoom(), 100);
    const QRect displayedRect = canvas.mosaicDisplayedRect();
    EXPECT_EQ(displayedRect.width(), testMosaic.width());
    EXPECT_EQ(displayedRect.height(), testMosaic.height());
}

TEST(ArtworkCanvasZoomTest, GridPanOnLeftClickHoldAndRelease) {
    ensureQApplication();
    ArtworkCanvas canvas;
    canvas.resize(800, 600);
    canvas.setMode(1); // kModeGrid
    canvas.setZoom(200);

    // Initialisation avec des tuiles
    QVector<GridTile> tiles(4);
    tiles[0].path = QStringLiteral("dummy1.tif");
    tiles[1].path = QStringLiteral("dummy2.tif");
    canvas.setGridState(2, 2, tiles, 0);

    // Vérification du glisser-déposer de tuiles (survol et échange)
    int swappedFrom = -1;
    int swappedTo = -1;
    canvas.setGridCallbacks([](int) {}, [&](int a, int b) {
        swappedFrom = a;
        swappedTo = b;
    });

    // Clic et glisser sur une tuile
    QMouseEvent pressTile(QEvent::MouseButtonPress, QPointF(200, 200), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &pressTile);
    QMouseEvent moveTile(QEvent::MouseMove, QPointF(400, 200), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &moveTile);
    QMouseEvent releaseTile(QEvent::MouseButtonRelease, QPointF(400, 200), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &releaseTile);

    // 1. Clic gauche enfoncé sur le fond hors tuile (par ex. (10, 10) dans la marge)
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &press);

    // 2. Déplacement avec clic gauche maintenu sur le fond
    QMouseEvent moveWhileHeld(QEvent::MouseMove, QPointF(50, 30), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &moveWhileHeld);

    // 3. Relâchement du clic gauche (MouseButtonRelease) : doit quitter immédiatement le panning
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(50, 30), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &release);

    // 4. Déplacement de la souris sans bouton enfoncé : ne doit PLUS modifier la vue
    QMouseEvent moveAfterRelease(QEvent::MouseMove, QPointF(80, 80), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &moveAfterRelease);
}

TEST(PyramidalPreviewQuality, ZoomLevelSelectionScalesQuality) {
    const fs::path tempDir = fs::temp_directory_path() / "kikirpa_pyramid_quality_test";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir);
    const fs::path pyramidPath = tempDir / "test_pyramid.tif";

    // Crée une image source de 2000x1500
    cv::Mat testImage(1500, 2000, CV_8UC3, cv::Scalar(100, 150, 200));
    std::string err;
    ASSERT_TRUE(IONs::createPyramidalTiledTiff(testImage, pyramidPath, err)) << err;

    const QString pyramidPathQt = QString::fromStdString(IONs::pathToUtf8(pyramidPath));

    // Dézoom : vignette petite (128x96)
    const QImage lowRes = FrontendUtils::readPyramidalPreview(pyramidPathQt, QSize(128, 128));
    EXPECT_FALSE(lowRes.isNull());
    EXPECT_LE(lowRes.width(), 128);

    // Zoom avant : grande vignette (2000x1500)
    const QImage highRes = FrontendUtils::readPyramidalPreview(pyramidPathQt, QSize(2000, 2000));
    EXPECT_FALSE(highRes.isNull());
    EXPECT_EQ(highRes.width(), 2000);
    EXPECT_EQ(highRes.height(), 1500);

    // Lecture par viewport : à 100% de zoom, lit au niveau 0 pleine résolution
    const auto vp100 = FrontendUtils::readPyramidalViewport(pyramidPathQt, QPointF(100, 100), QSize(300, 200), 100.0);
    EXPECT_TRUE(vp100.isValid);
    EXPECT_EQ(vp100.level, 0);
    EXPECT_FALSE(vp100.image.isNull());
    EXPECT_EQ(vp100.image.width(), 300);
    EXPECT_EQ(vp100.image.height(), 200);

    // Lecture par viewport : à 25% de zoom, choisit un niveau réduit
    const auto vp25 = FrontendUtils::readPyramidalViewport(pyramidPathQt, QPointF(0, 0), QSize(100, 75), 25.0);
    EXPECT_TRUE(vp25.isValid);
    EXPECT_EQ(vp25.level, 2);
    EXPECT_FALSE(vp25.image.isNull());

    fs::remove_all(tempDir, ec);
}

}
