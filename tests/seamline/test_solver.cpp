#include "algorithms/seamline/dynamic_programming_dijkstra/solver.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/dijkstra.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/dynamic_programming.h"

#include "test_maps.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

using namespace DynamicProgrammingDijkstraNs;

TEST(Solver, MethodNamesRoundTrip) {
    ASSERT_EQ(getAllMethods().size(), 3u);
    for (SolverMethod m : getAllMethods()) EXPECT_EQ(parseMethodName(getMethodName(m)), m);
    EXPECT_EQ(getMethodName(SolverMethod::DynamicProgramming), "DP");
    EXPECT_EQ(getMethodName(SolverMethod::Dijkstra8), "Dijkstra-8");
    EXPECT_EQ(getMethodName(SolverMethod::DynamicProgrammingDijkstraLocal), "DP+Dijkstra-local");
    EXPECT_THROW(parseMethodName("A*"), std::invalid_argument);
}

TEST(Solver, PathObjective) {
    const cv::Mat cost = testmaps::regular(14, 9, 7, 13);
    EXPECT_TRUE(std::isinf(computePathObjective(cost, {})));
    EXPECT_DOUBLE_EQ(computePathObjective(cost, findDijkstraPath(cost).path), 44.32010639674015);
}

TEST(Solver, SolveEdgeUsesTheRightSolver) {
    const cv::Mat cost = testmaps::maze(14, 12, 7, 13);
    const cv::Point start(0, 1), goal(10, 13);

    const SolveResult dp = solveEdge(cost, SolverMethod::DynamicProgramming, &start, &goal);
    EXPECT_EQ(dp.method, SolverMethod::DynamicProgramming);
    EXPECT_EQ(dp.path, findDynamicProgrammingPath(cost, &start, &goal).path);
    EXPECT_TRUE(std::isnan(dp.corridorRatio));

    const SolveResult dij = solveEdge(cost, SolverMethod::Dijkstra8, &start, &goal);
    EXPECT_EQ(dij.path, findDijkstraPath(cost, &start, &goal).path);
    EXPECT_GT(dij.pushCount, 0);
    EXPECT_TRUE(std::isnan(dij.seedSeconds));

    const SolveResult hyb = solveEdge(cost, SolverMethod::DynamicProgrammingDijkstraLocal, &start, &goal, 2);
    EXPECT_EQ(hyb.path, findDynamicProgrammingDijkstraLocalPath(cost, 2, &start, &goal).path);
    EXPECT_DOUBLE_EQ(hyb.objective, 17.700685424949224);
    EXPECT_GT(hyb.corridorRatio, 0.0);
    EXPECT_LT(hyb.corridorRatio, 1.0);
    EXPECT_GE(hyb.seedSeconds, 0.0);
    EXPECT_GE(hyb.refineSeconds, 0.0);

    for (const SolveResult* r : { &dp, &dij, &hyb }) {
        EXPECT_EQ(r->cellCount, 14 * 12);
        EXPECT_GE(r->wallSeconds, 0.0);
        EXPECT_GT(r->estimatedMemoryMegabytes, 0.0);
        EXPECT_NEAR(computePathObjective(cost, r->path), r->objective, 1e-9);
    }
    EXPECT_LE(dij.objective, hyb.objective);
    EXPECT_LE(hyb.objective, dp.objective);
}

TEST(Solver, MemoryEstimate) {
    const cv::Size s(100, 200);
    EXPECT_DOUBLE_EQ(estimateSolverMemoryMegabytes(SolverMethod::DynamicProgramming, s), 20000.0 * 20 / (1024.0 * 1024.0));
    EXPECT_DOUBLE_EQ(estimateSolverMemoryMegabytes(SolverMethod::Dijkstra8, s, 10), (20000.0 * 26 + 160) / (1024.0 * 1024.0));
    EXPECT_DOUBLE_EQ(estimateSolverMemoryMegabytes(SolverMethod::DynamicProgrammingDijkstraLocal, s, 10),
        estimateSolverMemoryMegabytes(SolverMethod::Dijkstra8, s, 10));
}

TEST(Solver, FreeEndpointsForEveryMethod) {
    const cv::Mat cost = testmaps::maze(14, 12, 7, 13);
    const SolveResult dp = solveEdge(cost, SolverMethod::DynamicProgramming);
    const SolveResult dij = solveEdge(cost, SolverMethod::Dijkstra8);
    const SolveResult hyb0 = solveEdge(cost, SolverMethod::DynamicProgrammingDijkstraLocal, nullptr, nullptr, 0);
    const SolveResult hyb8 = solveEdge(cost, SolverMethod::DynamicProgrammingDijkstraLocal, nullptr, nullptr, 8);
    EXPECT_DOUBLE_EQ(dp.objective, 42.987842712474624);
    EXPECT_DOUBLE_EQ(dij.objective, 4.365685424949239);
    EXPECT_DOUBLE_EQ(hyb0.objective, dp.objective);       // corridor nul : la graine DP
    EXPECT_DOUBLE_EQ(hyb8.objective, dij.objective);      // grand corridor : l'optimum
    EXPECT_LT(hyb0.corridorRatio, hyb8.corridorRatio);
    EXPECT_EQ(dp.pushCount, 0);
    EXPECT_EQ(dp.expandedCount, 14 * 12);
}

TEST(Solver, InvalidMethodThrows) {
    const SolverMethod bogus = static_cast<SolverMethod>(42);
    EXPECT_THROW(getMethodName(bogus), std::invalid_argument);
    EXPECT_THROW(solveEdge(cv::Mat(4, 4, CV_32F, cv::Scalar(1.0)), bogus), std::invalid_argument);
    EXPECT_THROW(parseMethodName(""), std::invalid_argument);
    EXPECT_THROW(parseMethodName("dp"), std::invalid_argument);      // sensible à la casse, comme le Python
}

TEST(Solver, FallsBackWhenDpCannotReachEndpoint) {
    const cv::Mat cost(10, 10, CV_32F, cv::Scalar(1.0));
    const cv::Point s(0, 0), g(9, 3);
    const SolveResult fallback = solveEdge(cost, SolverMethod::DynamicProgramming, &s, &g);
    ASSERT_FALSE(fallback.path.empty());
    EXPECT_EQ(fallback.path.front(), s);
    EXPECT_EQ(fallback.path.back(), g);

    EXPECT_NO_THROW(solveEdge(cost, SolverMethod::Dijkstra8, &s, &g));
    const cv::Point outside(20, 0);
    EXPECT_THROW(solveEdge(cost, SolverMethod::Dijkstra8, &outside), std::invalid_argument);
}
