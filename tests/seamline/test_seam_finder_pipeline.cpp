// Tests de SeamFinder::compute() : carte de coût (sections 5 / 5.0) + DP (section 6.1).
#include "algorithms/seamline/dp_dijkstra/seam_finder.hpp"
#include "algorithms/seamline/dp_dijkstra/cost_catalog.hpp"
#include "algorithms/seamline/dp_dijkstra/dynamic_programming.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>

using namespace seamlib;

namespace {

    // Overlap de 40 lignes x 48 colonnes : rampe verticale (aucun gradient horizontal).
    // `right` = `left` + 60 partout, sauf dans les colonnes 18 à 29 où les deux images coïncident.
    // La seamline doit donc passer dans cette bande. La bande est assez large pour que le flou
    // de grain_robust (sigma = 1,5 px) ne la remplisse pas.
    void make_overlaps(cv::Mat& left, cv::Mat& right) {
        left.create(40, 48, CV_8UC3);
        for (int y = 0; y < left.rows; ++y)
            left.row(y).setTo(cv::Scalar(100 + y, 90 + y, 80 + y));
        right = left + cv::Scalar(60, 60, 60);
        left.colRange(18, 30).copyTo(right.colRange(18, 30));
    }

    void expect_valid_seam(const std::vector<cv::Point>& seam, const cv::Size& size) {
        ASSERT_EQ(static_cast<int>(seam.size()), size.height);
        for (size_t i = 0; i < seam.size(); ++i) {
            EXPECT_EQ(seam[i].y, static_cast<int>(i));
            EXPECT_GE(seam[i].x, 0);
            EXPECT_LT(seam[i].x, size.width);
            if (i > 0) {
                EXPECT_LE(std::abs(seam[i].x - seam[i - 1].x), 1);
            }
        }
    }

}  // namespace

TEST(SeamFinderPipeline, EmptyInputGivesEmptySeam) {
    SeamFinder finder;
    EXPECT_TRUE(finder.compute(cv::Mat(), cv::Mat()).empty());
}

TEST(SeamFinderPipeline, DefaultModeFollowsTheMatchingBand) {
    cv::Mat left, right;
    make_overlaps(left, right);

    SeamFinder finder;
    const std::vector<cv::Point> seam = finder.compute(left, right);

    expect_valid_seam(seam, left.size());
    for (const cv::Point& p : seam) {
        EXPECT_GE(p.x, 18);
        EXPECT_LE(p.x, 29);
    }
}

TEST(SeamFinderPipeline, UserModesFollowTheMatchingBand) {
    cv::Mat left, right;
    make_overlaps(left, right);

    CostMapParams euclid_recall;
    euclid_recall.color_norm = "euclidian";
    euclid_recall.recall = true;

    CostMapParams percentile_caped;
    percentile_caped.normalization = "percentile";
    percentile_caped.treatment = "caped";

    const SeamFinder finders[] = {
        SeamFinder(CostFunction::ColorGradient, euclid_recall),
        SeamFinder(CostFunction::ColorGradient, percentile_caped),
        SeamFinder(CostFunction::ColorOnly),
        SeamFinder(CostFunction::GrainRobust),
        SeamFinder(CostFunction::HighlightPenalty),
        SeamFinder(CostFunction::CenterPrior),
    };

    for (SeamFinder finder : finders) {
        const std::vector<cv::Point> seam = finder.compute(left, right);
        expect_valid_seam(seam, left.size());
        for (const cv::Point& p : seam) {
            EXPECT_GE(p.x, 18) << cost_function_name(finder.cost_function);
            EXPECT_LE(p.x, 29) << cost_function_name(finder.cost_function);
        }
    }
}

TEST(SeamFinderPipeline, ComputeIsCostCatalogThenDynamicProgramming) {
    cv::Mat left, right;
    make_overlaps(left, right);

    CostMapParams p;
    p.treatment = "squared";
    p.recall = true;

    for (CostFunction fn : all_cost_functions()) {
        SeamFinder finder(fn, p);
        const std::vector<cv::Point> expected = dp_constrained(compute_cost(left, right, fn, p).treated).path;
        EXPECT_EQ(finder.compute(left, right), expected) << cost_function_name(fn);
    }
}

TEST(SeamFinderPipeline, SettingsCanBeChangedAfterConstruction) {
    cv::Mat left, right;
    make_overlaps(left, right);

    SeamFinder finder;
    finder.cost_function = CostFunction::GradientOnly;
    finder.params.treatment = "linear";

    CostMapParams p;
    p.treatment = "linear";
    const std::vector<cv::Point> expected =
        dp_constrained(compute_cost(left, right, CostFunction::GradientOnly, p).treated).path;
    EXPECT_EQ(finder.compute(left, right), expected);
}

TEST(SeamFinderPipeline, InvalidSettingThrows) {
    cv::Mat left, right;
    make_overlaps(left, right);

    SeamFinder finder;
    finder.params.treatment = "cubic";
    EXPECT_THROW(finder.compute(left, right), std::invalid_argument);
}

TEST(SeamFinderPipeline, DijkstraMethodsFollowTheMatchingBand) {
    cv::Mat left, right;
    make_overlaps(left, right);

    for (Method m : { Method::Dijkstra8, Method::DPDijkstraLocal }) {
        SeamFinder finder(CostFunction::ColorGradient, CostMapParams(), m);
        const std::vector<cv::Point> seam = finder.compute(left, right);
        ASSERT_FALSE(seam.empty()) << method_name(m);
        EXPECT_EQ(seam.front().y, 0) << method_name(m);
        EXPECT_EQ(seam.back().y, left.rows - 1) << method_name(m);
        for (const cv::Point& p : seam) {
            EXPECT_GE(p.x, 18) << method_name(m);
            EXPECT_LE(p.x, 29) << method_name(m);
        }
        const cv::Mat cost = compute_cost(left, right, CostFunction::ColorGradient, CostMapParams()).treated;
        EXPECT_EQ(seam, solve_edge(cost, m, nullptr, nullptr, finder.corridor_radius).path) << method_name(m);
    }
}

TEST(SeamFinderPipeline, CorridorRadiusIsUsed) {
    cv::Mat left, right;
    make_overlaps(left, right);

    SeamFinder finder(CostFunction::ColorGradient, CostMapParams(), Method::DPDijkstraLocal);
    EXPECT_EQ(finder.method, Method::DPDijkstraLocal);
    EXPECT_EQ(finder.corridor_radius, 18);
    finder.corridor_radius = 0;
    const cv::Mat cost = compute_cost(left, right, CostFunction::ColorGradient, CostMapParams()).treated;
    EXPECT_EQ(finder.compute(left, right), solve_edge(cost, Method::DPDijkstraLocal, nullptr, nullptr, 0).path);
    EXPECT_EQ(SeamFinder().method, Method::DP);
}
