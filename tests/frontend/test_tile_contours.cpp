#include "artworkcanvas.h"

#include <gtest/gtest.h>
#include <QtWidgets/QApplication>

namespace {

GridTile makeTile(const QString &path, bool empty = false)
{
    GridTile tile;
    tile.path = path;
    tile.empty = empty;
    return tile;
}

TEST(TileContoursTest, UsesTileFilenamesAndAccountsForOverlap)
{
    ArtworkCanvas canvas;
    const QVector<GridTile> tiles = {
        makeTile(QStringLiteral("tiles/tile_00.tif")),
        makeTile(QStringLiteral("tiles/tile_01.tif")),
        makeTile(QStringLiteral("tiles/tile_10.tif")),
        makeTile(QStringLiteral("tiles/tile_11.tif"))
    };
    canvas.setGridState(2, 2, tiles, 0);
    canvas.setOverlap(20, 20);

    const QVector<ArtworkCanvas::TileContour> contours =
        canvas.computeTileContours(QRectF(0.0, 0.0, 1800.0, 1800.0));

    ASSERT_EQ(contours.size(), 4);
    EXPECT_EQ(contours[0].tileId, QStringLiteral("tile_00.tif"));
    EXPECT_NEAR(contours[0].rect.width(), 1000.0, 1e-3);
    EXPECT_NEAR(contours[1].rect.x(), 800.0, 1e-3);
    EXPECT_EQ(contours[1].tileId, QStringLiteral("tile_01.tif"));
}

TEST(TileContoursTest, SkipsEmptyAndPathlessTiles)
{
    ArtworkCanvas canvas;
    const QVector<GridTile> tiles = {
        makeTile(QStringLiteral("tile_00.tif")),
        makeTile(QStringLiteral("tile_01.tif"), true),
        makeTile(QString()),
        makeTile(QStringLiteral("tile_11.tif"))
    };
    canvas.setGridState(2, 2, tiles, 0);

    const QVector<ArtworkCanvas::TileContour> contours =
        canvas.computeTileContours(QRectF(0.0, 0.0, 1800.0, 1800.0));

    ASSERT_EQ(contours.size(), 2);
    EXPECT_EQ(contours[0].tileId, QStringLiteral("tile_00.tif"));
    EXPECT_EQ(contours[1].tileId, QStringLiteral("tile_11.tif"));
}

TEST(TileContoursTest, UsesAbsoluteTilePositionsWhenAvailable)
{
    ArtworkCanvas canvas;
    canvas.setTilePositions({
        {QStringLiteral("tile_A.tif"), 0.0, 0.0, 400.0, 300.0},
        {QStringLiteral("tile_B.tif"), 320.0, 0.0, 400.0, 300.0}
    });

    const QVector<ArtworkCanvas::TileContour> contours =
        canvas.computeTileContours(QRectF(50.0, 50.0, 720.0, 300.0));

    ASSERT_EQ(contours.size(), 2);
    EXPECT_EQ(contours[1].tileId, QStringLiteral("tile_B.tif"));
    EXPECT_NEAR(contours[1].rect.x(), 370.0, 1e-3);
    EXPECT_NEAR(contours[0].rect.width(), 400.0, 1e-3);
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
