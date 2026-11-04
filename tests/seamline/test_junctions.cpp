#include "algorithms/seamline/dynamic_programming_dijkstra/junctions.hpp"
#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.hpp"
#include "algorithms/seamline/dynamic_programming_dijkstra/dijkstra.hpp"
#include "algorithms/seamline/dynamic_programming_dijkstra/dynamic_programming.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using namespace DynamicProgrammingDijkstraNS;

namespace {

    // Grille 2×2 (une seule jonction J_0_0) : mêmes cartes que le calcul de référence Python.
    SeamGrid maze_grid() {
        SeamGrid g;
        g.n_rows = 2;
        g.n_cols = 2;
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
        g.n_rows = 3;
        g.n_cols = 3;
        cv::RNG rng(7);
        auto rand_cost = [&](int h, int w) {
            cv::Mat c(h, w, CV_32F);
            rng.fill(c, cv::RNG::UNIFORM, 0.0, 5.0);
            return c;
        };
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 2; ++c) g.edges[edge_key('H', r, c)] = { rand_cost(60, 12), 12.0, 1.0 };
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c) g.edges[edge_key('V', r, c)] = { rand_cost(50, 10), 10.0, 1.0 };
        return g;
    }

}  // namespace

TEST(Junctions, EdgeKeyAndCounts) {
    EXPECT_EQ(edge_key('H', 2, 0), "H_2_0");
    EXPECT_EQ(edge_key('V', 10, 1), "V_10_1");
    SeamGrid g;
    g.n_rows = 5;
    g.n_cols = 3;
    EXPECT_EQ(g.n_edges(), 22);
    EXPECT_EQ(g.n_junctions(), 8);
}

TEST(Junctions, DirectionalDist) {
    const cv::Mat cost = testmaps::regular(14, 9, 7, 13);
    EXPECT_TRUE(cv::norm(directional_dist(cost, "top", Method::DP), dp_distance_top(cost), cv::NORM_INF) == 0.0);
    EXPECT_TRUE(cv::norm(directional_dist(cost, "top", Method::Dijkstra8), dijkstra_distance_map(cost), cv::NORM_INF) == 0.0);
    // L'hybride utilise les cartes DP.
    EXPECT_TRUE(cv::norm(directional_dist(cost, "top", Method::DPDijkstraLocal), dp_distance_top(cost), cv::NORM_INF) == 0.0);

    // "bottom" : distance depuis le bord bas = distance depuis le haut de la carte retournée, retournée.
    for (Method m : all_methods()) {
        cv::Mat flipped, expected;
        cv::flip(cost, flipped, 0);
        cv::flip(directional_dist(flipped, "top", m), expected, 0);
        EXPECT_TRUE(cv::norm(directional_dist(cost, "bottom", m), expected, cv::NORM_INF) == 0.0) << method_name(m);
    }
    EXPECT_THROW(directional_dist(cost, "left", Method::DP), std::invalid_argument);
}

TEST(Junctions, InputMapsAreNotModified) {
    const SeamGrid g = maze_grid();
    const SeamGrid ref = maze_grid();
    for (Method m : all_methods()) {
        const JunctionSet js = choose_all_junctions(g, m);
        solve_global_network(g, m, js);
        for (const auto& [key, e] : ref.edges)
            EXPECT_TRUE(cv::norm(g.edges.at(key).cost, e.cost, cv::NORM_INF) == 0.0) << key << " " << method_name(m);
    }
}

TEST(Junctions, ParityWithNotebook) {
    const SeamGrid g = maze_grid();
    const Junction dp = choose_junction_point(g, 0, 0, Method::DP);
    EXPECT_DOUBLE_EQ(dp.u, 3 * (1.0 / 40));
    EXPECT_DOUBLE_EQ(dp.v, 5 * (1.0 / 40));
    EXPECT_DOUBLE_EQ(dp.score, 77.91677984920742);
    EXPECT_EQ(dp.score_map.size(), cv::Size(41, 41));
    double mn = 0.0;
    cv::minMaxLoc(dp.score_map, &mn);
    EXPECT_DOUBLE_EQ(mn, dp.score);

    const Junction dij = choose_junction_point(g, 0, 0, Method::Dijkstra8);
    EXPECT_DOUBLE_EQ(dij.u, 3 * (1.0 / 40));
    EXPECT_DOUBLE_EQ(dij.v, 5 * (1.0 / 40));
    EXPECT_DOUBLE_EQ(dij.score, 10.331370849898478);

    const Junction coarse = choose_junction_point(g, 0, 0, Method::Dijkstra8, 7);
    EXPECT_DOUBLE_EQ(coarse.u, 1.0 / 6);
    EXPECT_DOUBLE_EQ(coarse.v, 1.0 / 6);
    EXPECT_DOUBLE_EQ(coarse.score, 13.486370849898478);

    // L'hybride réutilise les jonctions DP.
    const Junction hyb = choose_junction_point(g, 0, 0, Method::DPDijkstraLocal);
    EXPECT_EQ(hyb.u, dp.u);
    EXPECT_EQ(hyb.v, dp.v);
    EXPECT_EQ(hyb.score, dp.score);
}

TEST(Junctions, ChooseAllJunctions) {
    const SeamGrid g = random_grid();
    for (Method m : all_methods()) {
        const JunctionSet js = choose_all_junctions(g, m, 21);
        EXPECT_EQ(js.method, m);
        ASSERT_EQ(int(js.points.size()), g.n_junctions());
        for (const auto& [rc, J] : js.points) {
            const Junction alone = choose_junction_point(g, rc.first, rc.second, m, 21);
            EXPECT_EQ(J.u, alone.u);
            EXPECT_EQ(J.v, alone.v);
            EXPECT_GE(J.u, 0.0);
            EXPECT_LE(J.u, 1.0);
        }
    }
}

TEST(Junctions, MissingEdgeThrows) {
    SeamGrid g = maze_grid();
    g.edges.erase("V_0_1");
    EXPECT_THROW(choose_all_junctions(g, Method::DP), std::invalid_argument);
}

TEST(Junctions, EdgeEndpointsMatchHandComputation) {
    // J_0_0 = (u, v) = (0,075 ; 0,125). Profondeurs : 24 px × 0,5 = 12 (H) ; 32 px × 0,5 = 16 (V).
    const SeamGrid g = maze_grid();
    const JunctionSet js = choose_all_junctions(g, Method::DP);

    const Endpoints h00 = edge_endpoints(g, "H_0_0", js);   // carte 20×8 : J en bas
    EXPECT_FALSE(h00.start);
    ASSERT_TRUE(h00.goal);
    EXPECT_EQ(*h00.goal, cv::Point(1, 9));                  // x = round(0,125·7), y = round(8 + 0,075·11)

    const Endpoints h10 = edge_endpoints(g, "H_1_0", js);   // carte 18×7 : J en haut
    EXPECT_EQ(*h10.start, cv::Point(1, 1));
    EXPECT_FALSE(h10.goal);
    EXPECT_EQ(h10.goal_ptr(), nullptr);

    const Endpoints v00 = edge_endpoints(g, "V_0_0", js);   // carte transposée 16×9 : J à droite
    EXPECT_EQ(*v00.goal, cv::Point(1, 2));
    const Endpoints v01 = edge_endpoints(g, "V_0_1", js);   // carte transposée 15×10 : J à gauche
    EXPECT_EQ(*v01.start, cv::Point(1, 2));

    EXPECT_THROW(edge_endpoints(g, "X_0_0", js), std::invalid_argument);
    EXPECT_THROW(edge_endpoints(g, "H_0_0", JunctionSet()), std::invalid_argument);
}

TEST(Junctions, GlobalNetworkRespectsJunctions) {
    const SeamGrid g = random_grid();
    for (Method m : all_methods()) {
        const JunctionSet js = choose_all_junctions(g, m, 21);
        const auto seams = solve_global_network(g, m, js, 4);
        ASSERT_EQ(int(seams.size()), g.n_edges());
        for (const auto& [key, res] : seams) {
            const Endpoints ep = edge_endpoints(g, key, js);
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
        g.n_rows = 3;
        g.n_cols = 2;
        for (int r = 0; r < 3; ++r) g.edges[edge_key('H', r, 0)] = { testmaps::regular(30, 10, 3, 5), 10.0, 1.0 };
        for (int c = 0; c < 2; ++c) {
            g.edges[edge_key('V', 0, c)] = { testmaps::regular(20, 8, 2, 7), 6.0, 0.5 };
            g.edges[edge_key('V', 1, c)] = { testmaps::regular(20, 8, 2, 7), 10.0, 0.5 };
        }
        return g;
    }

    JunctionSet manual_junctions() {
        JunctionSet js;
        js.points[{ 0, 0 }].u = 0.5;
        js.points[{ 0, 0 }].v = 0.25;
        js.points[{ 1, 0 }].u = 1.0;
        js.points[{ 1, 0 }].v = 1.0;
        return js;
    }

}  // namespace

TEST(Junctions, EdgeEndpointsStartAndGoalHandComputed) {
    const SeamGrid g = grid_3x2();
    const JunctionSet js = manual_junctions();

    // H_0_0 : arrivée J_0_0, d = 6 → (round(0,25·9), round(24 + 0,5·5)) = (round(2,25), round(26,5)) = (2, 26)
    // (arrondi Python : 26,5 → 26, au pair le plus proche)
    const Endpoints h00 = edge_endpoints(g, "H_0_0", js);
    EXPECT_FALSE(h00.start);
    EXPECT_EQ(*h00.goal, cv::Point(2, 26));

    // H_1_0 : départ J_0_0 (d = 6) ET arrivée J_1_0 (d = 10)
    const Endpoints h10 = edge_endpoints(g, "H_1_0", js);
    EXPECT_EQ(*h10.start, cv::Point(2, 2));          // round(2,5) = 2
    EXPECT_EQ(*h10.goal, cv::Point(9, 29));          // (9, 20 + 9)
    EXPECT_EQ(h10.start_ptr(), &*h10.start);
    EXPECT_EQ(h10.goal_ptr(), &*h10.goal);

    const Endpoints h20 = edge_endpoints(g, "H_2_0", js);
    EXPECT_EQ(*h20.start, cv::Point(9, 9));
    EXPECT_FALSE(h20.goal);

    // Adjacences V (repère transposé), d = 5
    EXPECT_EQ(*edge_endpoints(g, "V_0_0", js).goal, cv::Point(4, 16));    // (round(3,5) = 4, round(15 + 1))
    EXPECT_FALSE(edge_endpoints(g, "V_0_0", js).start);
    EXPECT_EQ(*edge_endpoints(g, "V_0_1", js).start, cv::Point(4, 1));
    EXPECT_FALSE(edge_endpoints(g, "V_0_1", js).goal);
    EXPECT_EQ(*edge_endpoints(g, "V_1_0", js).goal, cv::Point(7, 19));
    EXPECT_EQ(*edge_endpoints(g, "V_1_1", js).start, cv::Point(7, 4));

    // Le réseau résolu passe bien par ces points.
    const auto seams = solve_global_network(g, Method::Dijkstra8, js);
    EXPECT_EQ(seams.at("H_1_0").path.front(), cv::Point(2, 2));
    EXPECT_EQ(seams.at("H_1_0").path.back(), cv::Point(9, 29));
}

TEST(Junctions, InvalidKeysAndMissingJunction) {
    const SeamGrid g = grid_3x2();
    const JunctionSet js = manual_junctions();
    for (const std::string key : { "H_0", "H00_0", "h_0_0", "", "V_a_0", "H_0_x" })
        EXPECT_THROW(edge_endpoints(g, key, js), std::invalid_argument) << key;
    EXPECT_THROW(edge_endpoints(g, "H_5_0", js), std::invalid_argument);    // clé bien formée mais absente

    JunctionSet partial = js;
    partial.points.erase({ 1, 0 });
    EXPECT_THROW(edge_endpoints(g, "H_1_0", partial), std::invalid_argument);
    EXPECT_THROW(solve_global_network(g, Method::DP, partial), std::invalid_argument);
}

TEST(Junctions, EmptyCostMapAndGridResThrow) {
    SeamGrid g = maze_grid();
    EXPECT_THROW(choose_junction_point(g, 0, 0, Method::DP, 0), std::invalid_argument);
    g.edges["H_1_0"].cost = cv::Mat();
    EXPECT_THROW(choose_junction_point(g, 0, 0, Method::DP), std::invalid_argument);
}

TEST(Junctions, GridResOneTestsOnlyTheCorner) {
    const Junction j = choose_junction_point(maze_grid(), 0, 0, Method::DP, 1);
    EXPECT_EQ(j.u, 0.0);
    EXPECT_EQ(j.v, 0.0);
    EXPECT_EQ(j.score_map.size(), cv::Size(1, 1));
    EXPECT_EQ(j.score_map.at<double>(0, 0), j.score);
}

TEST(Junctions, TimingsAndEmptyGrid) {
    const JunctionSet js = choose_all_junctions(maze_grid(), Method::Dijkstra8);
    EXPECT_GE(js.total_time_s, 0.0);
    for (const auto& kv : js.points) EXPECT_GE(kv.second.time_s, 0.0);

    SeamGrid empty;                                           // pas de bloc 2×2 : aucune jonction
    empty.n_rows = 1;
    empty.n_cols = 1;
    EXPECT_TRUE(choose_all_junctions(empty, Method::DP).points.empty());
    EXPECT_TRUE(solve_global_network(empty, Method::DP, JunctionSet()).empty());
}

// --- De bout en bout : images → compute_cost → jonctions → réseau ------------------------------------

TEST(Junctions, EndToEndFromImages) {
    // Grille 2×2 : pour chaque adjacence, deux overlaps RGB qui ne coïncident que dans une bande
    // centrale. Adjacences V : les overlaps haut/bas (18 × 60) sont transposés AVANT compute_cost.
    cv::RNG rng(3);
    auto overlaps = [&](int h, int w, cv::Mat& a, cv::Mat& b) {
        a.create(h, w, CV_8UC3);
        rng.fill(a, cv::RNG::UNIFORM, 40, 200);
        b = a + cv::Scalar(50, 50, 50);
        a.colRange(w / 3, 2 * w / 3).copyTo(b.colRange(w / 3, 2 * w / 3));
    };

    SeamGrid g;
    g.n_rows = 2;
    g.n_cols = 2;
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
        g.edges[key] = { compute_cost(a, b, CostFunction::ColorGradient).treated, 18.0, 1.0 };
    }

    for (Method m : all_methods()) {
        const JunctionSet js = choose_all_junctions(g, m);
        const auto seams = solve_global_network(g, m, js);
        ASSERT_EQ(seams.size(), 4u) << method_name(m);
        for (const auto& [key, res] : seams) {
            const Endpoints ep = edge_endpoints(g, key, js);
            const SolveResult manual = solve_edge(g.edges.at(key).cost, m, ep.start_ptr(), ep.goal_ptr());
            EXPECT_EQ(res.path, manual.path) << key << " " << method_name(m);
            EXPECT_DOUBLE_EQ(res.objective, manual.objective) << key;
        }
    }
}
