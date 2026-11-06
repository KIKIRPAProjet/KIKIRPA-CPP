#include "algorithms/seamline/dynamic_programming_dijkstra/dijkstra.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"

#include "test_maps.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>
#include <vector>

using namespace DynamicProgrammingDijkstraNs;

namespace {

    void expect_8_connected(const std::vector<cv::Point>& path) {
        for (size_t i = 1; i < path.size(); ++i) {
            EXPECT_LE(std::abs(path[i].x - path[i - 1].x), 1);
            EXPECT_LE(std::abs(path[i].y - path[i - 1].y), 1);
            EXPECT_NE(path[i], path[i - 1]);
        }
    }

}  // namespace

// --- Dijkstra-8 ---------------------------------------------------------------------------------

TEST(Dijkstra, ParityWithNotebookFreeEndpoints) {
    const PathResult r = findDijkstraPath(testmaps::regular(14, 9, 7, 13));
    const std::vector<cv::Point> expected = { {0, 0}, {0, 1}, {0, 2}, {1, 3}, {1, 4}, {1, 5}, {2, 6},
                                              {3, 7}, {3, 8}, {3, 9}, {4, 10}, {3, 11}, {3, 12}, {4, 13} };
    EXPECT_EQ(r.path, expected);
    EXPECT_DOUBLE_EQ(r.objective, 44.32010639674015);
    EXPECT_EQ(r.expandedCount, 118);
    EXPECT_EQ(r.pushCount, 177);
    EXPECT_EQ(r.maximumHeapSize, 21);
    EXPECT_TRUE(r.settledMask.empty());
}

TEST(Dijkstra, ParityWithNotebookImposedEndpoints) {
    const cv::Point start(2, 0), goal(6, 13);
    const PathResult r = findDijkstraPath(testmaps::regular(14, 9, 7, 13), &start, &goal);
    const std::vector<cv::Point> expected = { {2, 0}, {3, 1}, {4, 2}, {4, 3}, {4, 4}, {5, 5}, {5, 6},
                                              {5, 7}, {6, 8}, {7, 9}, {6, 10}, {6, 11}, {6, 12}, {6, 13} };
    EXPECT_EQ(r.path, expected);
    EXPECT_DOUBLE_EQ(r.objective, 50.79142674029979);
    EXPECT_EQ(r.expandedCount, 125);
}

TEST(Dijkstra, ParityWithNotebookFollowsLateralCorridor) {
    // Le couloir horizontal du labyrinthe n'est accessible qu'avec des pas latéraux.
    const PathResult r = findDijkstraPath(testmaps::maze(14, 12, 7, 13));
    const std::vector<cv::Point> expected = { {1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 6},
                                              {2, 7}, {3, 7}, {4, 7}, {5, 7}, {6, 7}, {7, 7}, {8, 7}, {9, 7},
                                              {10, 8}, {10, 9}, {10, 10}, {10, 11}, {10, 12}, {10, 13} };
    EXPECT_EQ(r.path, expected);
    EXPECT_DOUBLE_EQ(r.objective, 4.365685424949239);
    EXPECT_EQ(r.expandedCount, 27);
    EXPECT_EQ(r.pushCount, 120);
    EXPECT_EQ(r.maximumHeapSize, 95);
    EXPECT_LT(r.objective, findDynamicProgrammingPath(testmaps::maze(14, 12, 7, 13)).objective);
}

TEST(Dijkstra, NeverWorseThanDpAndObjectiveConsistent) {
    cv::Mat cost(50, 30, CV_32F);
    cv::randu(cost, 0.0, 5.0);
    const PathResult r = findDijkstraPath(cost);
    EXPECT_EQ(r.path.front().y, 0);
    EXPECT_EQ(r.path.back().y, cost.rows - 1);
    expect_8_connected(r.path);
    EXPECT_NEAR(computePathObjective(cost, r.path), r.objective, 1e-9);
    EXPECT_LE(r.objective, findDynamicProgrammingPath(cost).objective + 1e-9);
}

TEST(Dijkstra, DistanceMapMatchesFreeObjective) {
    const cv::Mat cost = testmaps::regular(14, 9, 7, 13);
    const cv::Mat d = computeDijkstraDistanceMap(cost);
    EXPECT_EQ(d.type(), CV_64F);
    EXPECT_EQ(d.size(), cost.size());
    double mn = 0.0;
    cv::minMaxLoc(d.row(d.rows - 1), &mn);
    EXPECT_DOUBLE_EQ(mn, findDijkstraPath(cost).objective);
    EXPECT_DOUBLE_EQ(d.at<double>(13, 0), 46.613826099314075);   // valeurs du notebook
    EXPECT_DOUBLE_EQ(d.at<double>(13, 8), 46.08173590577917);
}

TEST(Dijkstra, AllowedMaskAndSettled) {
    cv::Mat cost(20, 15, CV_32F);
    cv::randu(cost, 0.0, 5.0);
    cv::Mat allowed = cv::Mat::zeros(cost.size(), CV_8U);
    allowed.colRange(5, 9).setTo(1);
    const PathResult r = findDijkstraPath(cost, nullptr, nullptr, allowed, true);
    for (const cv::Point& p : r.path) EXPECT_TRUE(allowed.at<uchar>(p));
    ASSERT_FALSE(r.settledMask.empty());
    EXPECT_EQ(cv::countNonZero(r.settledMask), r.expandedCount);
    EXPECT_EQ(cv::countNonZero(r.settledMask & (allowed == 0)), 0);
}

TEST(Dijkstra, InvalidOrUnreachableEndpointsThrow) {
    const cv::Mat cost(10, 10, CV_32F, cv::Scalar(1.0));
    const cv::Point outside(10, 0);
    EXPECT_THROW(findDijkstraPath(cost, &outside), std::invalid_argument);
    EXPECT_THROW(findDijkstraPath(cost, nullptr, &outside), std::invalid_argument);

    cv::Mat allowed = cv::Mat::ones(cost.size(), CV_8U);
    allowed.row(5).setTo(0);                  // mur infranchissable
    EXPECT_THROW(findDijkstraPath(cost, nullptr, nullptr, allowed), std::runtime_error);
}

// --- Dijkstra amélioré : DP → Dijkstra local ------------------------------------------------------

TEST(DpDijkstraLocal, CorridorMaskIsDilatedPath) {
    const std::vector<cv::Point> path = { {4, 0}, {4, 1}, {5, 2}, {5, 3} };
    const cv::Mat m = buildCorridorMask(path, cv::Size(12, 4), 2);
    EXPECT_EQ(m.type(), CV_8U);
    EXPECT_EQ(m.at<uchar>(0, 2), 1);
    EXPECT_EQ(m.at<uchar>(0, 6), 1);
    EXPECT_EQ(m.at<uchar>(0, 1), 0);
    EXPECT_EQ(m.at<uchar>(3, 7), 1);
    EXPECT_EQ(m.at<uchar>(3, 8), 0);
}

TEST(DpDijkstraLocal, CorridorKeepsUsefulBordersReachable) {
    const std::vector<cv::Point> path = { {4, 0}, {4, 1}, {4, 2}, {4, 3}, {4, 4} };
    const cv::Mat free_ends = buildHybridCorridor(path, cv::Size(12, 5), 1);
    EXPECT_EQ(cv::countNonZero(free_ends.row(0)), 12);
    EXPECT_EQ(cv::countNonZero(free_ends.row(4)), 12);
    EXPECT_EQ(cv::countNonZero(free_ends.row(2)), 3);

    const cv::Point start(11, 0), goal(0, 4);
    const cv::Mat fixed = buildHybridCorridor(path, cv::Size(12, 5), 1, &start, &goal);
    EXPECT_EQ(fixed.at<uchar>(start), 1);
    EXPECT_EQ(fixed.at<uchar>(goal), 1);
    EXPECT_EQ(cv::countNonZero(fixed.row(0)), 4);
}

TEST(DpDijkstraLocal, ParityWithNotebookByRadius) {
    const cv::Mat cost = testmaps::maze(14, 12, 7, 13);
    const double dp = findDynamicProgrammingPath(cost).objective;
    // Petit corridor : la graine DP ne peut pas être améliorée.
    EXPECT_DOUBLE_EQ(findDynamicProgrammingDijkstraLocalPath(cost, 1).objective, 42.987842712474624);
    EXPECT_DOUBLE_EQ(findDynamicProgrammingDijkstraLocalPath(cost, 1).objective, dp);
    // Corridor moyen : amélioration partielle.
    const PathResult r7 = findDynamicProgrammingDijkstraLocalPath(cost, 7);
    EXPECT_DOUBLE_EQ(r7.objective, 42.93679328294829);
    EXPECT_EQ(r7.expandedCount, 133);
    EXPECT_EQ(r7.pushCount, 228);
    EXPECT_EQ(r7.maximumHeapSize, 71);
    // Grand corridor : on retrouve l'optimum de Dijkstra-8.
    const PathResult r8 = findDynamicProgrammingDijkstraLocalPath(cost, 8);
    EXPECT_DOUBLE_EQ(r8.objective, 4.365685424949239);
    EXPECT_EQ(r8.path, findDijkstraPath(cost).path);
}

TEST(DpDijkstraLocal, ParityWithNotebookImposedEndpoints) {
    const cv::Mat cost = testmaps::maze(14, 12, 7, 13);
    const cv::Point start(0, 1), goal(10, 13);
    EXPECT_DOUBLE_EQ(findDynamicProgrammingPath(cost, &start, &goal).objective, 89.96582276684097);
    const PathResult r = findDynamicProgrammingDijkstraLocalPath(cost, 2, &start, &goal);
    EXPECT_EQ(r.path.front(), start);
    EXPECT_EQ(r.path.back(), goal);
    EXPECT_DOUBLE_EQ(r.objective, 17.700685424949224);
    EXPECT_EQ(r.expandedCount, 27);
}

TEST(DpDijkstraLocal, BetweenDijkstraAndDp) {
    cv::Mat cost(60, 40, CV_32F);
    cv::randu(cost, 0.0, 5.0);
    const double dp = findDynamicProgrammingPath(cost).objective;
    const double dij = findDijkstraPath(cost).objective;
    for (int radius : { 0, 2, 5, 18 }) {
        const PathResult r = findDynamicProgrammingDijkstraLocalPath(cost, radius);
        EXPECT_LE(r.objective, dp + 1e-9) << radius;
        EXPECT_GE(r.objective, dij - 1e-9) << radius;
        expect_8_connected(r.path);
    }
}

// --- Cas limites et erreurs -------------------------------------------------------------------------

TEST(Dijkstra, AcceptsAnyNumericTypeAndSingleImposedEndpoint) {
    const cv::Mat cost64 = testmaps::regular(14, 9, 7, 13);
    cv::Mat cost32;
    cost64.convertTo(cost32, CV_32F);
    const cv::Mat cost32_back = [&] { cv::Mat c; cost32.convertTo(c, CV_64F); return c; }();
    EXPECT_EQ(findDijkstraPath(cost32).path, findDijkstraPath(cost32_back).path);

    const cv::Point start(5, 0);
    const PathResult rs = findDijkstraPath(cost64, &start);              // départ seul imposé
    EXPECT_EQ(rs.path.front(), start);
    EXPECT_EQ(rs.path.back().y, 13);

    const cv::Point goal(2, 13);
    const PathResult rg = findDijkstraPath(cost64, nullptr, &goal);      // arrivée seule imposée
    EXPECT_EQ(rg.path.front().y, 0);
    EXPECT_EQ(rg.path.back(), goal);

    const cv::Point mid(4, 6);                                         // arrivée au milieu de la carte
    const PathResult rm = findDijkstraPath(cost64, nullptr, &mid);
    EXPECT_EQ(rm.path.back(), mid);
    EXPECT_NEAR(computePathObjective(cost64, rm.path), rm.objective, 1e-9);
}

TEST(Dijkstra, SinglePixelAndSingleRow) {
    const cv::Mat one(1, 1, CV_64F, cv::Scalar(2.5));
    const PathResult r = findDijkstraPath(one);
    ASSERT_EQ(r.path.size(), 1u);
    EXPECT_DOUBLE_EQ(r.objective, 2.5);

    cv::Mat row = (cv::Mat_<double>(1, 4) << 3.0, 1.0, 2.0, 0.5);
    EXPECT_EQ(findDijkstraPath(row).path, std::vector<cv::Point>({ {3, 0} }));   // le moins cher de la ligne
}

TEST(Dijkstra, InvalidInputsThrow) {
    EXPECT_THROW(findDijkstraPath(cv::Mat()), cv::Exception);                                  // carte vide
    EXPECT_THROW(findDijkstraPath(cv::Mat(5, 5, CV_32FC3, cv::Scalar::all(1))), cv::Exception); // 3 canaux
    const cv::Mat cost(5, 5, CV_32F, cv::Scalar(1.0));
    EXPECT_THROW(findDijkstraPath(cost, nullptr, nullptr, cv::Mat::ones(4, 5, CV_8U)), cv::Exception);   // masque mal dimensionné
    EXPECT_THROW(findDijkstraPath(cost, nullptr, nullptr, cv::Mat::ones(5, 5, CV_32F)), cv::Exception);  // masque mal typé
    const cv::Point negative(-1, 0);
    EXPECT_THROW(findDijkstraPath(cost, &negative), std::invalid_argument);
    EXPECT_THROW(computeDijkstraDistanceMap(cv::Mat()), cv::Exception);
}

TEST(Dijkstra, EndpointOutsideMaskHasNoPath) {
    const cv::Mat cost(6, 6, CV_32F, cv::Scalar(1.0));
    cv::Mat allowed = cv::Mat::ones(cost.size(), CV_8U);
    allowed.at<uchar>(0, 2) = 0;
    allowed.at<uchar>(5, 4) = 0;
    const cv::Point start(2, 0), goal(4, 5);
    EXPECT_THROW(findDijkstraPath(cost, &start, nullptr, allowed), std::runtime_error);
    EXPECT_THROW(findDijkstraPath(cost, nullptr, &goal, allowed), std::runtime_error);
}

TEST(DpDijkstraLocal, RadiusZeroKeepsTheDpPath) {
    const cv::Mat cost = testmaps::maze(14, 12, 7, 13);
    const cv::Mat corridor = buildHybridCorridor(findDynamicProgrammingPath(cost).path, cost.size(), 0);
    EXPECT_EQ(cv::countNonZero(corridor), 14 + 2 * 12 - 2);   // le chemin DP + lignes du haut et du bas
    EXPECT_DOUBLE_EQ(findDynamicProgrammingDijkstraLocalPath(cost, 0).objective, findDynamicProgrammingPath(cost).objective);
}
