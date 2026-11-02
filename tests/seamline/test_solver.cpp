#include "algorithms/seamline/dp_dijkstra/solver.hpp"
#include "algorithms/seamline/dp_dijkstra/dijkstra.hpp"
#include "algorithms/seamline/dp_dijkstra/dynamic_programming.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

using namespace seamlib;

TEST(Solver, MethodNamesRoundTrip) {
    ASSERT_EQ(all_methods().size(), 3u);
    for (Method m : all_methods()) EXPECT_EQ(method_from_name(method_name(m)), m);
    EXPECT_EQ(method_name(Method::DP), "DP");
    EXPECT_EQ(method_name(Method::Dijkstra8), "Dijkstra-8");
    EXPECT_EQ(method_name(Method::DPDijkstraLocal), "DP+Dijkstra-local");
    EXPECT_THROW(method_from_name("A*"), std::invalid_argument);
}

TEST(Solver, PathObjective) {
    const cv::Mat cost = testmaps::regular(14, 9, 7, 13);
    EXPECT_TRUE(std::isinf(path_objective(cost, {})));
    EXPECT_DOUBLE_EQ(path_objective(cost, dijkstra_path(cost).path), 44.32010639674015);
}

TEST(Solver, SolveEdgeUsesTheRightSolver) {
    const cv::Mat cost = testmaps::maze(14, 12, 7, 13);
    const cv::Point start(0, 1), goal(10, 13);

    const SolveResult dp = solve_edge(cost, Method::DP, &start, &goal);
    EXPECT_EQ(dp.method, Method::DP);
    EXPECT_EQ(dp.path, dp_constrained(cost, &start, &goal).path);
    EXPECT_TRUE(std::isnan(dp.corridor_ratio));

    const SolveResult dij = solve_edge(cost, Method::Dijkstra8, &start, &goal);
    EXPECT_EQ(dij.path, dijkstra_path(cost, &start, &goal).path);
    EXPECT_GT(dij.pushes, 0);
    EXPECT_TRUE(std::isnan(dij.seed_s));

    const SolveResult hyb = solve_edge(cost, Method::DPDijkstraLocal, &start, &goal, 2);
    EXPECT_EQ(hyb.path, dp_dijkstra_local(cost, 2, &start, &goal).path);
    EXPECT_DOUBLE_EQ(hyb.objective, 17.700685424949224);
    EXPECT_GT(hyb.corridor_ratio, 0.0);
    EXPECT_LT(hyb.corridor_ratio, 1.0);
    EXPECT_GE(hyb.seed_s, 0.0);
    EXPECT_GE(hyb.refine_s, 0.0);

    for (const SolveResult* r : { &dp, &dij, &hyb }) {
        EXPECT_EQ(r->cells, 14 * 12);
        EXPECT_GE(r->wall_s, 0.0);
        EXPECT_GT(r->mem_est_mb, 0.0);
        EXPECT_NEAR(path_objective(cost, r->path), r->objective, 1e-9);
    }
    EXPECT_LE(dij.objective, hyb.objective);
    EXPECT_LE(hyb.objective, dp.objective);
}

TEST(Solver, MemoryEstimate) {
    const cv::Size s(100, 200);
    EXPECT_DOUBLE_EQ(estimate_solver_memory_mb(Method::DP, s), 20000.0 * 20 / (1024.0 * 1024.0));
    EXPECT_DOUBLE_EQ(estimate_solver_memory_mb(Method::Dijkstra8, s, 10), (20000.0 * 26 + 160) / (1024.0 * 1024.0));
    EXPECT_DOUBLE_EQ(estimate_solver_memory_mb(Method::DPDijkstraLocal, s, 10),
        estimate_solver_memory_mb(Method::Dijkstra8, s, 10));
}

TEST(Solver, FreeEndpointsForEveryMethod) {
    const cv::Mat cost = testmaps::maze(14, 12, 7, 13);
    const SolveResult dp = solve_edge(cost, Method::DP);
    const SolveResult dij = solve_edge(cost, Method::Dijkstra8);
    const SolveResult hyb0 = solve_edge(cost, Method::DPDijkstraLocal, nullptr, nullptr, 0);
    const SolveResult hyb8 = solve_edge(cost, Method::DPDijkstraLocal, nullptr, nullptr, 8);
    EXPECT_DOUBLE_EQ(dp.objective, 42.987842712474624);
    EXPECT_DOUBLE_EQ(dij.objective, 4.365685424949239);
    EXPECT_DOUBLE_EQ(hyb0.objective, dp.objective);       // corridor nul : la graine DP
    EXPECT_DOUBLE_EQ(hyb8.objective, dij.objective);      // grand corridor : l'optimum
    EXPECT_LT(hyb0.corridor_ratio, hyb8.corridor_ratio);
    EXPECT_EQ(dp.pushes, 0);
    EXPECT_EQ(dp.expanded, 14 * 12);
}

TEST(Solver, InvalidMethodThrows) {
    const Method bogus = static_cast<Method>(42);
    EXPECT_THROW(method_name(bogus), std::invalid_argument);
    EXPECT_THROW(solve_edge(cv::Mat(4, 4, CV_32F, cv::Scalar(1.0)), bogus), std::invalid_argument);
    EXPECT_THROW(method_from_name(""), std::invalid_argument);
    EXPECT_THROW(method_from_name("dp"), std::invalid_argument);      // sensible à la casse, comme le Python
}

TEST(Solver, FallsBackWhenDpCannotReachEndpoint) {
    const cv::Mat cost(10, 10, CV_32F, cv::Scalar(1.0));
    const cv::Point s(0, 0), g(9, 3);
    const SolveResult fallback = solve_edge(cost, Method::DP, &s, &g);
    ASSERT_FALSE(fallback.path.empty());
    EXPECT_EQ(fallback.path.front(), s);
    EXPECT_EQ(fallback.path.back(), g);

    EXPECT_NO_THROW(solve_edge(cost, Method::Dijkstra8, &s, &g));
    const cv::Point outside(20, 0);
    EXPECT_THROW(solve_edge(cost, Method::Dijkstra8, &outside), std::invalid_argument);
}
