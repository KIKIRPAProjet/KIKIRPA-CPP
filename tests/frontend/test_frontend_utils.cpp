// tests/frontend/test_frontend_utils.cpp
#include "artworkcanvas.h"
#include "frontendutils.h"

#include "core/io/pyramidal_tiff_writer.h"
#include "core/io/unicode_path.h"

#include <gtest/gtest.h>
#include <opencv2/core/mat.hpp>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace {

constexpr double ZOOM_FULL = 100.0;
constexpr double ZOOM_TWELVE = 12.0;
constexpr double ZOOM_TWELVE_POINT_FOUR = 12.4;
constexpr double ZOOM_TWO = 2.0;
constexpr double ZOOM_QUARTER = 0.25;
constexpr double ZOOM_TWO_TENTHS = 0.2;
constexpr double ZOOM_ONE_POINT_SIX = 1.6;

constexpr int TEST_IMG_SIZE = 256;
constexpr int CANVAS_WIDTH = 400;
constexpr int CANVAS_HEIGHT = 300;
constexpr int TIMEOUT_MS = 3000;
constexpr int EVENT_LOOP_MAX_MS = 50;
constexpr int SLEEP_MS = 20;

void ensureQApplication() {
    if (!QApplication::instance()) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static int argc = 1;
        static char appName[] = "test_frontend";
        static char* argv[] = {appName, nullptr};
        static QApplication app(argc, argv);
    }
}

TEST(FrontendUtilsTestClass, FormatsZoomWithDecimalsAndIntegers) {
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_FULL).toStdString(), "100 %");
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_TWELVE).toStdString(), "12 %");
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_TWELVE_POINT_FOUR).toStdString(), "12,4 %");
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_TWO).toStdString(), "2 %");
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_QUARTER).toStdString(), "0,25 %");
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_TWO_TENTHS).toStdString(), "0,2 %");
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_ONE_POINT_SIX).toStdString(), "1,6 %");
}

TEST(ArtworkCanvasTestClass, OpeningMosaicInGridModeFitsAndLoadsWhenSwitchingToExport) {
    ensureQApplication();

    const auto tiffPath = std::filesystem::temp_directory_path() / "kikirpa_test_canvas_mosaic.tif";
    const cv::Mat testMat(TEST_IMG_SIZE, TEST_IMG_SIZE, CV_8UC3, cv::Scalar(120, 140, 160));
    std::string err;
    ASSERT_TRUE(IONs::createPyramidalTiledTiff(testMat, tiffPath, err)) << err;

    {
        ArtworkCanvas canvas;
        canvas.resize(CANVAS_WIDTH, CANVAS_HEIGHT);
        canvas.setMode(FrontendUtils::kModeGrid);
        EXPECT_EQ(canvas.mode(), FrontendUtils::kModeGrid);

        QString openError;
        const QString qPath = QString::fromStdString(IONs::pathToUtf8(tiffPath));
        ASSERT_TRUE(canvas.setMosaicTiff(qPath, &openError)) << openError.toStdString();

        // Le canevas est toujours en mode Grille
        EXPECT_EQ(canvas.mode(), FrontendUtils::kModeGrid);

        // L'opérateur (ou la fin du calcul) bascule en mode Export
        canvas.setMode(FrontendUtils::kModeExport);
        EXPECT_EQ(canvas.mode(), FrontendUtils::kModeExport);

        // L'ajustement du zoom a été fait automatiquement
        EXPECT_GT(canvas.zoomPercentValue(), 0.0);

        // On attend que la lecture asynchrone de la région se termine
        QElapsedTimer timer;
        timer.start();
        while (!canvas.isMosaicRegionLoaded() && timer.elapsed() < TIMEOUT_MS) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, EVENT_LOOP_MAX_MS);
            std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_MS));
        }

        EXPECT_TRUE(canvas.isMosaicRegionLoaded());
    }

    std::filesystem::remove(tiffPath);
}

} // namespace
