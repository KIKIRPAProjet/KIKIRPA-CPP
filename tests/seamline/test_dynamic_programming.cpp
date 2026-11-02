#include "algorithms/seamline/dp_dijkstra/dynamic_programming.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <stdexcept>

using namespace seamlib;

namespace {

    // Recalcule l'objectif d'un chemin avec la même règle de pas que le solveur.
    double objective_of(const cv::Mat& cost32, const std::vector<cv::Point>& path) {
        cv::Mat cost;
        cost32.convertTo(cost, CV_64F);
        double total = cost.at<double>(path[0].y, path[0].x);
        for (size_t i = 0; i + 1 < path.size(); ++i)
            total += edge_weight(cost, path[i].y, path[i].x, path[i + 1].y, path[i + 1].x);
        return total;
    }

}  // namespace

TEST(DynamicProgramming, FollowsCheapColumn) {
    cv::Mat cost(8, 5, CV_32F, cv::Scalar(10.0));
    cost.col(3).setTo(1.0);
    const PathResult r = dp_constrained(cost);
    ASSERT_EQ(r.path.size(), 8u);
    for (const cv::Point& p : r.path) EXPECT_EQ(p.x, 3);
    EXPECT_DOUBLE_EQ(r.objective, 8.0);       // 1 + 7 pas verticaux à (1 + 1) / 2
    EXPECT_EQ(r.expanded, 8 * 5);
}

TEST(DynamicProgramming, UniformCostKeepsFirstMinimum) {
    // Tous les chemins droits se valent : np.argmin garde la première colonne.
    const cv::Mat cost(4, 6, CV_32F, cv::Scalar(1.0));
    const PathResult r = dp_constrained(cost);
    for (const cv::Point& p : r.path) EXPECT_EQ(p.x, 0);
    EXPECT_DOUBLE_EQ(r.objective, 4.0);
}

TEST(DynamicProgramming, PathIsMonotoneAndObjectiveConsistent) {
    cv::Mat cost(60, 25, CV_32F);
    cv::randu(cost, 0.0, 5.0);
    const PathResult r = dp_constrained(cost);
    ASSERT_EQ(int(r.path.size()), cost.rows);
    for (size_t i = 1; i < r.path.size(); ++i) {
        EXPECT_EQ(r.path[i].y, r.path[i - 1].y + 1);
        EXPECT_LE(std::abs(r.path[i].x - r.path[i - 1].x), 1);
    }
    EXPECT_NEAR(objective_of(cost, r.path), r.objective, 1e-9);
}

TEST(DynamicProgramming, ImposedEndpointsAreRespected) {
    cv::Mat cost(30, 20, CV_32F);
    cv::randu(cost, 0.0, 5.0);
    const cv::Point start(5, 2), goal(9, 25);
    const PathResult r = dp_constrained(cost, &start, &goal);
    EXPECT_EQ(r.path.front(), start);
    EXPECT_EQ(r.path.back(), goal);
    EXPECT_EQ(r.expanded, (25 - 2 + 1) * 20);
    // Borne valide : la DP libre sur les MÊMES lignes (2 à 25), pas sur toute la hauteur.
    EXPECT_GE(r.objective, dp_constrained(cost.rowRange(2, 26)).objective - 1e-12);
}

TEST(DynamicProgramming, InvalidOrUnreachableEndpointsThrow) {
    const cv::Mat cost(10, 10, CV_32F, cv::Scalar(1.0));
    const cv::Point start(0, 5), goal(0, 2);
    EXPECT_THROW(dp_constrained(cost, &start, &goal), std::invalid_argument);
    const cv::Point s2(0, 0), g2(9, 3);   // 9 colonnes en 3 lignes : impossible
    EXPECT_THROW(dp_constrained(cost, &s2, &g2), std::runtime_error);
}

TEST(DynamicProgramming, DistanceTopMatchesFreeObjective) {
    cv::Mat cost(40, 15, CV_32F);
    cv::randu(cost, 0.0, 3.0);
    const cv::Mat d = dp_distance_top(cost);
    EXPECT_EQ(d.type(), CV_64F);
    double mn = 0.0;
    cv::minMaxLoc(d.row(d.rows - 1), &mn);
    EXPECT_NEAR(mn, dp_constrained(cost).objective, 1e-9);
}

TEST(DynamicProgramming, DijkstraOnlyFieldsStayEmpty) {
    cv::Mat cost(10, 6, CV_32F);
    cv::randu(cost, 0.0, 3.0);
    const PathResult r = dp_constrained(cost);
    EXPECT_EQ(r.pushes, 0);
    EXPECT_EQ(r.max_heap, 0);
    EXPECT_TRUE(r.settled.empty());
}
