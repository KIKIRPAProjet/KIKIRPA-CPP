// tests/frontend/test_image_conversion.cpp
// L'ordre des canaux entre OpenCV et Qt, à la frontière de l'interface.
//
// Depuis le 24/11/2026 le TIFF pyramidal du cache contient ce qu'il déclare, donc du RGB (core/io/pyramid_cache.h),
// et matToQImage doit employer Format_RGB888 / Format_RGBA8888 et non Format_BGR888 / Format_ARGB32. Une erreur ici
// ne casse rien : elle produit un aperçu où le rouge et le bleu sont échangés, ce qu'aucune assertion ne voit et ce
// qu'un relecteur ne remarque que sur une image dont il connaît les couleurs.
//
// Ce contrat était tenu par ImageLayerLoaderTestClass.ConvertsColorTiffToQImage, parti avec le revert de la #128
// (PR #253, 26/11/2026) : c'est pourtant lui qui avait attrapé la régression le 24/11. Ces tests le remplacent, sans
// rien devoir à la superposition des couches.
#include "core/io/pyramidal_tiff_writer.h"
#include "core/io/unicode_path.h"
#include "frontendutils.h"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/matx.hpp>

#include <string>

namespace {

    constexpr int IMAGE_WIDTH = 32;
    constexpr int IMAGE_HEIGHT = 24;
    // Trois valeurs distinctes et éloignées : un canal échangé se voit, et le passage 16 -> 8 bits (division par
    // 256) les rend telles quelles.
    constexpr int RED_LEVEL = 16;
    constexpr int GREEN_LEVEL = 32;
    constexpr int BLUE_LEVEL = 192;
    constexpr int ALPHA_LEVEL = 128;
    constexpr int BITS_PER_BYTE_SHIFT = 256;   // 16 bits -> 8 bits : matToQImage divise par 256

    cv::Mat makeRgb16(int channels) {
        const int type = channels == 4 ? CV_16UC4 : CV_16UC3;
        const cv::Scalar colour = channels == 4
            ? cv::Scalar(RED_LEVEL * BITS_PER_BYTE_SHIFT, GREEN_LEVEL * BITS_PER_BYTE_SHIFT,
                         BLUE_LEVEL * BITS_PER_BYTE_SHIFT, ALPHA_LEVEL * BITS_PER_BYTE_SHIFT)
            : cv::Scalar(RED_LEVEL * BITS_PER_BYTE_SHIFT, GREEN_LEVEL * BITS_PER_BYTE_SHIFT,
                         BLUE_LEVEL * BITS_PER_BYTE_SHIFT);
        return {IMAGE_HEIGHT, IMAGE_WIDTH, type, colour};
    }

}

// Le premier canal de la matrice est le ROUGE du pixel affiché. Si matToQImage repassait à Format_BGR888, cette
// attente deviendrait QColor(BLUE_LEVEL, GREEN_LEVEL, RED_LEVEL) : c'est tout l'écart que la faute produit.
TEST(ImageConversionTest, MatToQImageKeepsTheDeclaredRgbOrder) {
    const QImage image = FrontendUtils::matToQImage(makeRgb16(3));

    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.size(), QSize(IMAGE_WIDTH, IMAGE_HEIGHT));
    EXPECT_EQ(image.pixelColor(0, 0), QColor(RED_LEVEL, GREEN_LEVEL, BLUE_LEVEL));
    EXPECT_EQ(image.pixelColor(IMAGE_WIDTH - 1, IMAGE_HEIGHT - 1), QColor(RED_LEVEL, GREEN_LEVEL, BLUE_LEVEL));
}

// Même chose à quatre canaux, où le piège est plus discret : Format_ARGB32 range les octets B, G, R, A en mémoire,
// donc une image RGBA lue comme de l'ARGB32 échange les canaux ET décale l'alpha.
TEST(ImageConversionTest, MatToQImageKeepsTheDeclaredRgbaOrder) {
    const QImage image = FrontendUtils::matToQImage(makeRgb16(4));

    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.pixelColor(0, 0), QColor(RED_LEVEL, GREEN_LEVEL, BLUE_LEVEL, ALPHA_LEVEL));
}

TEST(ImageConversionTest, MatToQImageRefusesWhatItCannotRepresent) {
    EXPECT_TRUE(FrontendUtils::matToQImage({}).isNull());
    EXPECT_TRUE(FrontendUtils::matToQImage(cv::Mat(IMAGE_HEIGHT, IMAGE_WIDTH, CV_32FC3)).isNull());
}

// Le chemin complet, celui que l'interface emprunte : un TIFF pyramidal écrit avec des pixels RGB, relu par
// readPyramidalPreview. `sourcePath` vide = le niveau 0 est cherché dans le cache, ce que le header autorise.
TEST(ImageConversionTest, PyramidalPreviewKeepsTheRgbOfTheCache) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString pyramidPath = directory.filePath(QStringLiteral("pyramide.tif"));
    std::string errorMessage;
    ASSERT_TRUE(IONs::createPyramidalTiledTiff(makeRgb16(3), IONs::pathFromUtf8(pyramidPath.toStdString()),
                                               errorMessage))
        << errorMessage;

    const QImage preview =
        FrontendUtils::readPyramidalPreview(QString(), pyramidPath, QSize(IMAGE_WIDTH, IMAGE_HEIGHT));

    ASSERT_FALSE(preview.isNull());
    EXPECT_EQ(preview.pixelColor(0, 0), QColor(RED_LEVEL, GREEN_LEVEL, BLUE_LEVEL));
}
