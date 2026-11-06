#include "algorithms/seamline/dynamic_programming_dijkstra/junctions.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/dijkstra.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/dynamic_programming.h"

#include "test_maps.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using namespace DynamicProgrammingDijkstraNs;

namespace {

    // Grille 2×2 (une seule jonction J_0_0) : mêmes cartes que le calcul de référence Python.
    SeamGrid maze_grid() {
        SeamGrid g;
        g.rowCount = 2;
        g.columnCount = 2;
        g.edges["H_0_0"] = { testmaps::maze(20, 8, 3, 5), 30.0, 0.5 };
        g.edges["H_1_0"] = { testmaps::maze(18, 7, 5, 2), 34.0, 0.5 };
        g.edges["V_0_0"] = { testmaps::maze(16, 9, 2, 7), 22.0, 0.5 };
        g.edges["V_0_1"] = { testmaps::maze(15, 10, 4, 3), 26.0, 0.5 };
        return g;
    }

    // Grille 3×3 aléatoire : 12 adjacences, 4 jonctions. Comme sur les vraies données, les cartes
    // sont longues devant la profondeur des overlaps : les extrémités imposées restent atteignables par la DP.
    SeamGrid random_grid() {
        SeamGrid g;
        g.rowCount = 3;
        g.columnCount = 3;
        cv::RNG rng(7);
        auto rand_cost = [&](int h, int w) {
            cv::Mat c(h, w, CV_32F);
            rng.fill(c, cv::RNG::UNIFORM, 0.0, 5.0);
            return c;
        };
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 2; ++c) g.edges[buildEdgeKey('H', r, c)] = { rand_cost(60, 12), 12.0, 1.0 };
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c) g.edges[buildEdgeKey('V', r, c)] = { rand_cost(50, 10), 10.0, 1.0 };
        return g;
    }

}  // namespace

TEST(Junctions, EdgeKeyAndCounts) {
    EXPECT_EQ(buildEdgeKey('H', 2, 0), "H_2_0");
    EXPECT_EQ(buildEdgeKey('V', 10, 1), "V_10_1");
    SeamGrid g;
    g.rowCount = 5;
    g.columnCount = 3;
    EXPECT_EQ(g.getEdgeCount(), 22);
    EXPECT_EQ(g.getJunctionCount(), 8);
}

TEST(Junctions, DirectionalDist) {
    const cv::Mat cost = testmaps::regular(14, 9, 7, 13);
    EXPECT_TRUE(cv::norm(computeDirectionalDistance(cost, "top", SolverMethod::DynamicProgramming), computeDynamicProgrammingDistanceFromTop(cost), cv::NORM_INF) == 0.0);
    EXPECT_TRUE(cv::norm(computeDirectionalDistance(cost, "top", SolverMethod::Dijkstra8), computeDijkstraDistanceMap(cost), cv::NORM_INF) == 0.0);
    // L'hybride utilise les cartes DP.
    EXPECT_TRUE(cv::norm(computeDirectionalDistance(cost, "top", SolverMethod::DynamicProgrammingDijkstraLocal), computeDynamicProgrammingDistanceFromTop(cost), cv::NORM_INF) == 0.0);

    // "bottom" : distance depuis le bord bas = distance depuis le haut de la carte retournée, retournée.
    for (SolverMethod m : getAllMethods()) {
        cv::Mat flipped, expected;
        cv::flip(cost, flipped, 0);
        cv::flip(computeDirectionalDistance(flipped, "top", m), expected, 0);
        EXPECT_TRUE(cv::norm(computeDirectionalDistance(cost, "bottom", m), expected, cv::NORM_INF) == 0.0) << getMethodName(m);
    }
    EXPECT_THROW(computeDirectionalDistance(cost, "left", SolverMethod::DynamicProgramming), std::invalid_argument);
}

TEST(Junctions, InputMapsAreNotModified) {
    const SeamGrid g = maze_grid();
    const SeamGrid ref = maze_grid();
    for (SolverMethod m : getAllMethods()) {
        const JunctionSet js = chooseAllJunctions(g, m);
        solveGlobalNetwork(g, m, js);
        for (const auto& [key, e] : ref.edges)
            EXPECT_TRUE(cv::norm(g.edges.at(key).cost, e.cost, cv::NORM_INF) == 0.0) << key << " " << getMethodName(m);
    }
}

TEST(Junctions, ParityWithNotebook) {
    const SeamGrid g = maze_grid();
    const Junction dp = chooseJunctionPoint(g, 0, 0, SolverMethod::DynamicProgramming);
    EXPECT_DOUBLE_EQ(dp.normalizedRow, 3 * (1.0 / 40));
    EXPECT_DOUBLE_EQ(dp.normalizedColumn, 5 * (1.0 / 40));
    EXPECT_DOUBLE_EQ(dp.score, 77.91677984920742);
    EXPECT_EQ(dp.scoreMap.size(), cv::Size(41, 41));
    double mn = 0.0;
    cv::minMaxLoc(dp.scoreMap, &mn);
    EXPECT_DOUBLE_EQ(mn, dp.score);

    const Junction dij = chooseJunctionPoint(g, 0, 0, SolverMethod::Dijkstra8);
    EXPECT_DOUBLE_EQ(dij.normalizedRow, 3 * (1.0 / 40));
    EXPECT_DOUBLE_EQ(dij.normalizedColumn, 5 * (1.0 / 40));
    EXPECT_DOUBLE_EQ(dij.score, 10.331370849898478);

    const Junction coarse = chooseJunctionPoint(g, 0, 0, SolverMethod::Dijkstra8, 7);
    EXPECT_DOUBLE_EQ(coarse.normalizedRow, 1.0 / 6);
    EXPECT_DOUBLE_EQ(coarse.normalizedColumn, 1.0 / 6);
    EXPECT_DOUBLE_EQ(coarse.score, 13.486370849898478);

    // L'hybride réutilise les jonctions DP.
    const Junction hyb = chooseJunctionPoint(g, 0, 0, SolverMethod::DynamicProgrammingDijkstraLocal);
    EXPECT_EQ(hyb.normalizedRow, dp.normalizedRow);
    EXPECT_EQ(hyb.normalizedColumn, dp.normalizedColumn);
    EXPECT_EQ(hyb.score, dp.score);
}

TEST(Junctions, ChooseAllJunctions) {
    const SeamGrid g = random_grid();
    for (SolverMethod m : getAllMethods()) {
        const JunctionSet js = chooseAllJunctions(g, m, 21);
        EXPECT_EQ(js.method, m);
        ASSERT_EQ(int(js.points.size()), g.getJunctionCount());
        for (const auto& [rc, J] : js.points) {
            const Junction alone = chooseJunctionPoint(g, rc.first, rc.second, m, 21);
            EXPECT_EQ(J.normalizedRow, alone.normalizedRow);
            EXPECT_EQ(J.normalizedColumn, alone.normalizedColumn);
            EXPECT_GE(J.normalizedRow, 0.0);
            EXPECT_LE(J.normalizedRow, 1.0);
        }
    }
}

TEST(Junctions, MissingEdgeThrows) {
    SeamGrid g = maze_grid();
    g.edges.erase("V_0_1");
    EXPECT_THROW(chooseAllJunctions(g, SolverMethod::DynamicProgramming), std::invalid_argument);
}

TEST(Junctions, EdgeEndpointsMatchHandComputation) {
    // J_0_0 = (u, v) = (0,075 ; 0,125). Profondeurs : 24 px × 0,5 = 12 (H) ; 32 px × 0,5 = 16 (V).
    const SeamGrid g = maze_grid();
    const JunctionSet js = chooseAllJunctions(g, SolverMethod::DynamicProgramming);

    const Endpoints h00 = computeEdgeEndpoints(g, "H_0_0", js);   // carte 20×8 : J en bas
    EXPECT_FALSE(h00.start);
    ASSERT_TRUE(h00.goal);
    EXPECT_EQ(*h00.goal, cv::Point(1, 9));                  // x = round(0,125·7), y = round(8 + 0,075·11)

    const Endpoints h10 = computeEdgeEndpoints(g, "H_1_0", js);   // carte 18×7 : J en haut
    EXPECT_EQ(*h10.start, cv::Point(1, 1));
    EXPECT_FALSE(h10.goal);
    EXPECT_EQ(h10.getGoalPointer(), nullptr);

    const Endpoints v00 = computeEdgeEndpoints(g, "V_0_0", js);   // carte transposée 16×9 : J à droite
    EXPECT_EQ(*v00.goal, cv::Point(1, 2));
    const Endpoints v01 = computeEdgeEndpoints(g, "V_0_1", js);   // carte transposée 15×10 : J à gauche
    EXPECT_EQ(*v01.start, cv::Point(1, 2));

    EXPECT_THROW(computeEdgeEndpoints(g, "X_0_0", js), std::invalid_argument);
    EXPECT_THROW(computeEdgeEndpoints(g, "H_0_0", JunctionSet()), std::invalid_argument);
}

TEST(Junctions, GlobalNetworkRespectsJunctions) {
    const SeamGrid g = random_grid();
    for (SolverMethod m : getAllMethods()) {
        const JunctionSet js = chooseAllJunctions(g, m, 21);
        const auto seams = solveGlobalNetwork(g, m, js, 4);
        ASSERT_EQ(int(seams.size()), g.getEdgeCount());
        for (const auto& [key, res] : seams) {
            const Endpoints ep = computeEdgeEndpoints(g, key, js);
            ASSERT_FALSE(res.path.empty()) << key;
            EXPECT_EQ(res.method, m);
            if (ep.start) EXPECT_EQ(res.path.front(), *ep.start) << key;
            else EXPECT_EQ(res.path.front().y, 0) << key;
            if (ep.goal) EXPECT_EQ(res.path.back(), *ep.goal) << key;
            else EXPECT_EQ(res.path.back().y, g.edges.at(key).cost.rows - 1) << key;
        }
    }
}

// --- Extrémités : grille 3×2 calculée à la main ---------------------------------------------------

namespace {

    // 3 lignes × 2 colonnes : H_0_0, H_1_0, H_2_0 (cartes 30×10, overlap 10 px, échelle 1)
    // et V_0_0, V_0_1 (overlap 6 px), V_1_0, V_1_1 (overlap 10 px), cartes 20×8 à l'échelle 0,5.
    // Profondeurs : H_*  → V_0_* : 6 px, V_1_* : 10 px ;  V_* → H_* : 10 px × 0,5 = 5.
    SeamGrid grid_3x2() {
        SeamGrid g;
        g.rowCount = 3;
        g.columnCount = 2;
        for (int r = 0; r < 3; ++r) g.edges[buildEdgeKey('H', r, 0)] = { testmaps::regular(30, 10, 3, 5), 10.0, 1.0 };
        for (int c = 0; c < 2; ++c) {
            g.edges[buildEdgeKey('V', 0, c)] = { testmaps::regular(20, 8, 2, 7), 6.0, 0.5 };
            g.edges[buildEdgeKey('V', 1, c)] = { testmaps::regular(20, 8, 2, 7), 10.0, 0.5 };
        }
        return g;
    }

    JunctionSet manual_junctions() {
        JunctionSet js;
        js.points[{ 0, 0 }].normalizedRow = 0.5;
        js.points[{ 0, 0 }].normalizedColumn = 0.25;
        js.points[{ 1, 0 }].normalizedRow = 1.0;
        js.points[{ 1, 0 }].normalizedColumn = 1.0;
        return js;
    }

}  // namespace

TEST(Junctions, EdgeEndpointsStartAndGoalHandComputed) {
    const SeamGrid g = grid_3x2();
    const JunctionSet js = manual_junctions();

    // H_0_0 : arrivée J_0_0, d = 6 → (round(0,25·9), round(24 + 0,5·5)) = (round(2,25), round(26,5)) = (2, 26)
    // (arrondi Python : 26,5 → 26, au pair le plus proche)
    const Endpoints h00 = computeEdgeEndpoints(g, "H_0_0", js);
    EXPECT_FALSE(h00.start);
    EXPECT_EQ(*h00.goal, cv::Point(2, 26));

    // H_1_0 : départ J_0_0 (d = 6) ET arrivée J_1_0 (d = 10)
    const Endpoints h10 = computeEdgeEndpoints(g, "H_1_0", js);
    EXPECT_EQ(*h10.start, cv::Point(2, 2));          // round(2,5) = 2
    EXPECT_EQ(*h10.goal, cv::Point(9, 29));          // (9, 20 + 9)
    EXPECT_EQ(h10.getStartPointer(), &*h10.start);
    EXPECT_EQ(h10.getGoalPointer(), &*h10.goal);

    const Endpoints h20 = computeEdgeEndpoints(g, "H_2_0", js);
    EXPECT_EQ(*h20.start, cv::Point(9, 9));
    EXPECT_FALSE(h20.goal);

    // Adjacences V (repère transposé), d = 5
    EXPECT_EQ(*computeEdgeEndpoints(g, "V_0_0", js).goal, cv::Point(4, 16));    // (round(3,5) = 4, round(15 + 1))
    EXPECT_FALSE(computeEdgeEndpoints(g, "V_0_0", js).start);
    EXPECT_EQ(*computeEdgeEndpoints(g, "V_0_1", js).start, cv::Point(4, 1));
    EXPECT_FALSE(computeEdgeEndpoints(g, "V_0_1", js).goal);
    EXPECT_EQ(*computeEdgeEndpoints(g, "V_1_0", js).goal, cv::Point(7, 19));
    EXPECT_EQ(*computeEdgeEndpoints(g, "V_1_1", js).start, cv::Point(7, 4));

    // Le réseau résolu passe bien par ces points.
    const auto seams = solveGlobalNetwork(g, SolverMethod::Dijkstra8, js);
    EXPECT_EQ(seams.at("H_1_0").path.front(), cv::Point(2, 2));
    EXPECT_EQ(seams.at("H_1_0").path.back(), cv::Point(9, 29));
}

TEST(Junctions, InvalidKeysAndMissingJunction) {
    const SeamGrid g = grid_3x2();
    const JunctionSet js = manual_junctions();
    for (const std::string key : { "H_0", "H00_0", "h_0_0", "", "V_a_0", "H_0_x" })
        EXPECT_THROW(computeEdgeEndpoints(g, key, js), std::invalid_argument) << key;
    EXPECT_THROW(computeEdgeEndpoints(g, "H_5_0", js), std::invalid_argument);    // clé bien formée mais absente

    JunctionSet partial = js;
    partial.points.erase({ 1, 0 });
    EXPECT_THROW(computeEdgeEndpoints(g, "H_1_0", partial), std::invalid_argument);
    EXPECT_THROW(solveGlobalNetwork(g, SolverMethod::DynamicProgramming, partial), std::invalid_argument);
}

TEST(Junctions, EmptyCostMapAndGridResThrow) {
    SeamGrid g = maze_grid();
    EXPECT_THROW(chooseJunctionPoint(g, 0, 0, SolverMethod::DynamicProgramming, 0), std::invalid_argument);
    g.edges["H_1_0"].cost = cv::Mat();
    EXPECT_THROW(chooseJunctionPoint(g, 0, 0, SolverMethod::DynamicProgramming), std::invalid_argument);
}

TEST(Junctions, GridResOneTestsOnlyTheCorner) {
    const Junction j = chooseJunctionPoint(maze_grid(), 0, 0, SolverMethod::DynamicProgramming, 1);
    EXPECT_EQ(j.normalizedRow, 0.0);
    EXPECT_EQ(j.normalizedColumn, 0.0);
    EXPECT_EQ(j.scoreMap.size(), cv::Size(1, 1));
    EXPECT_EQ(j.scoreMap.at<double>(0, 0), j.score);
}

TEST(Junctions, TimingsAndEmptyGrid) {
    const JunctionSet js = chooseAllJunctions(maze_grid(), SolverMethod::Dijkstra8);
    EXPECT_GE(js.totalTimeSeconds, 0.0);
    for (const auto& kv : js.points) EXPECT_GE(kv.second.timeSeconds, 0.0);

    SeamGrid empty;                                           // pas de bloc 2×2 : aucune jonction
    empty.rowCount = 1;
    empty.columnCount = 1;
    EXPECT_TRUE(chooseAllJunctions(empty, SolverMethod::DynamicProgramming).points.empty());
    EXPECT_TRUE(solveGlobalNetwork(empty, SolverMethod::DynamicProgramming, JunctionSet()).empty());
}

// --- De bout en bout : images → computeCost → jonctions → réseau ------------------------------------

TEST(Junctions, EndToEndFromImages) {
    // Grille 2×2 : pour chaque adjacence, deux overlaps RGB qui ne coïncident que dans une bande
    // centrale. Adjacences V : les overlaps haut/bas (18 × 60) sont transposés AVANT computeCost.
    cv::RNG rng(3);
    auto overlaps = [&](int h, int w, cv::Mat& a, cv::Mat& b) {
        a.create(h, w, CV_8UC3);
        rng.fill(a, cv::RNG::UNIFORM, 40, 200);
        b = a + cv::Scalar(50, 50, 50);
        a.colRange(w / 3, 2 * w / 3).copyTo(b.colRange(w / 3, 2 * w / 3));
    };

    SeamGrid g;
    g.rowCount = 2;
    g.columnCount = 2;
    for (const std::string key : { "H_0_0", "H_1_0", "V_0_0", "V_0_1" }) {
        cv::Mat a, b;
        overlaps(60, 18, a, b);
        if (key[0] == 'V') {
            cv::Mat top, bottom;                  // overlaps haut/bas tels qu'on les lit dans les tuiles
            cv::transpose(a, top);
            cv::transpose(b, bottom);
            cv::transpose(top, a);                // repère solveur : transposition avant le coût
            cv::transpose(bottom, b);
        }
        g.edges[key] = { computeCost(a, b, CostFunction::ColorGradient).treated, 18.0, 1.0 };
    }

    for (SolverMethod m : getAllMethods()) {
        const JunctionSet js = chooseAllJunctions(g, m);
        const auto seams = solveGlobalNetwork(g, m, js);
        ASSERT_EQ(seams.size(), 4u) << getMethodName(m);
        for (const auto& [key, res] : seams) {
            const Endpoints ep = computeEdgeEndpoints(g, key, js);
            const SolveResult manual = solveEdge(g.edges.at(key).cost, m, ep.getStartPointer(), ep.getGoalPointer());
            EXPECT_EQ(res.path, manual.path) << key << " " << getMethodName(m);
            EXPECT_DOUBLE_EQ(res.objective, manual.objective) << key;
        }
    }
}
