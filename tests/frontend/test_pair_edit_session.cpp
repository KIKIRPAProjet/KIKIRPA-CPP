// tests/frontend/test_pair_edit_session.cpp
// Édition des points d'une paire (mode Alignement) : décalage médian, écart par point, déplacement de B, défaire.
#include "pair_edit_session.h"

#include <gtest/gtest.h>

#include <QPointF>
#include <QVector>

namespace {

// Trois points cohérents (B décalée de (800, 2) dans A) et un point faux de 10 px.
PairEditSession sessionWithOneWrongPoint()
{
    PairEditSession session;
    session.start({{900.0, 100.0}, {950.0, 400.0}, {1000.0, 700.0}, {980.0, 250.0}},
                  {{100.0, 98.0}, {150.0, 398.0}, {200.0, 698.0}, {170.0, 248.0}}, QPointF(0.0, 0.0));
    return session;
}

}

TEST(PairEditSessionTest, OffsetIsTheMedianAndTheWrongPointHasTheLargestResidual)
{
    const PairEditSession session = sessionWithOneWrongPoint();
    EXPECT_DOUBLE_EQ(session.offset().x(), 800.0);
    EXPECT_DOUBLE_EQ(session.offset().y(), 2.0);
    EXPECT_NEAR(session.residual(3), 10.0, 1e-9);
    EXPECT_NEAR(session.residual(0), 0.0, 1e-9);
    EXPECT_EQ(session.indicesByResidual().first(), 3);
}

TEST(PairEditSessionTest, RemovingAPointCanBeUndoneAndRedone)
{
    PairEditSession session = sessionWithOneWrongPoint();
    session.select(3);
    session.remove(3);
    EXPECT_EQ(session.count(), 3);
    EXPECT_EQ(session.selected(), -1);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(session.count(), 4);
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(session.count(), 3);
    EXPECT_FALSE(session.redo());
}

TEST(PairEditSessionTest, NudgeMovesBAndKeepsResiduals)
{
    PairEditSession session = sessionWithOneWrongPoint();
    session.nudge(QPointF(1.0, -10.0));
    EXPECT_DOUBLE_EQ(session.offset().x(), 801.0);
    EXPECT_DOUBLE_EQ(session.offset().y(), -8.0);
    EXPECT_NEAR(session.residual(3), 10.0, 1e-9);
}

TEST(PairEditSessionTest, WithoutPointsTheTypedOffsetIsKeptAndNudged)
{
    PairEditSession session;
    session.start({}, {}, QPointF(812.0, -3.0));
    session.nudge(QPointF(-1.0, 0.0));
    EXPECT_DOUBLE_EQ(session.offset().x(), 811.0);

    PairEditSession single;
    single.start({{900.0, 100.0}}, {{100.0, 98.0}}, QPointF(0.0, 0.0));
    single.remove(0);
    EXPECT_DOUBLE_EQ(single.offset().x(), 800.0);   // B reste là où elle était
}

TEST(PairEditSessionTest, MovingAPointIsOneUndoStep)
{
    PairEditSession session = sessionWithOneWrongPoint();
    session.beginMove();
    session.moveTo(3, false, QPointF(175.0, 248.0));
    session.moveTo(3, false, QPointF(180.0, 248.0));
    EXPECT_NEAR(session.residual(3), 0.0, 1e-9);
    ASSERT_TRUE(session.undo());
    EXPECT_NEAR(session.residual(3), 10.0, 1e-9);
    EXPECT_FALSE(session.undo());
}

TEST(PairEditSessionTest, AddSelectsTheNewPointAndStopClearsEverything)
{
    PairEditSession session = sessionWithOneWrongPoint();
    session.add(QPointF(1100.0, 500.0), QPointF(300.0, 498.0));
    EXPECT_EQ(session.selected(), 4);
    session.stop();
    EXPECT_FALSE(session.isActive());
    EXPECT_EQ(session.count(), 0);
    EXPECT_FALSE(session.canUndo());
}
