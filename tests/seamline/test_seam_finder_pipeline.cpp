// Tests de SeamFinderClass::compute() : carte de coût (sections 5 / 5.0) + DP (section 6.1).
#include "algorithms/seamline/dynamic_programming_dijkstra/seam_finder.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h"
#include "algorithms/seamline/dynamic_programming_dijkstra/dynamic_programming.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>

using namespace DynamicProgrammingDijkstraNs;

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
    SeamFinderClass finder;
    EXPECT_TRUE(finder.computeSeam(cv::Mat(), cv::Mat()).empty());
}

TEST(SeamFinderPipeline, DefaultModeFollowsTheMatchingBand) {
    cv::Mat left, right;
    make_overlaps(left, right);

    SeamFinderClass finder;
    const std::vector<cv::Point> seam = finder.computeSeam(left, right);

    expect_valid_seam(seam, left.size());
    for (const cv::Point& p : seam) {
        EXPECT_GE(p.x, 18);
        EXPECT_LE(p.x, 29);
    }
}

TEST(SeamFinderPipeline, UserModesFollowTheMatchingBand) {
    cv::Mat left, right;
    make_overlaps(left, right);

    CostMapParameters euclid_recall;
    euclid_recall.colorNorm = "euclidian";
    euclid_recall.recall = true;

    CostMapParameters percentile_caped;
    percentile_caped.normalization = "percentile";
    percentile_caped.treatment = "caped";

    const SeamFinderClass finders[] = {
        SeamFinderClass(CostFunction::ColorGradient, euclid_recall),
        SeamFinderClass(CostFunction::ColorGradient, percentile_caped),
        SeamFinderClass(CostFunction::ColorOnly),
        SeamFinderClass(CostFunction::GrainRobust),
        SeamFinderClass(CostFunction::HighlightPenalty),
        SeamFinderClass(CostFunction::CenterPrior),
    };

    for (SeamFinderClass finder : finders) {
        const std::vector<cv::Point> seam = finder.computeSeam(left, right);
        expect_valid_seam(seam, left.size());
        for (const cv::Point& p : seam) {
            EXPECT_GE(p.x, 18) << getCostFunctionName(finder.costFunction);
            EXPECT_LE(p.x, 29) << getCostFunctionName(finder.costFunction);
        }
    }
}

TEST(SeamFinderPipeline, ComputeIsCostCatalogThenDynamicProgramming) {
    cv::Mat left, right;
    make_overlaps(left, right);

    CostMapParameters p;
    p.treatment = "squared";
    p.recall = true;

    for (CostFunction fn : getAllCostFunctions()) {
        SeamFinderClass finder(fn, p);
        const std::vector<cv::Point> expected = findDynamicProgrammingPath(computeCost(left, right, fn, p).treated).path;
        EXPECT_EQ(finder.computeSeam(left, right), expected) << getCostFunctionName(fn);
    }
}

TEST(SeamFinderPipeline, SettingsCanBeChangedAfterConstruction) {
    cv::Mat left, right;
    make_overlaps(left, right);

    SeamFinderClass finder;
    finder.costFunction = CostFunction::GradientOnly;
    finder.parameters.treatment = "linear";

    CostMapParameters p;
    p.treatment = "linear";
    const std::vector<cv::Point> expected =
        findDynamicProgrammingPath(computeCost(left, right, CostFunction::GradientOnly, p).treated).path;
    EXPECT_EQ(finder.computeSeam(left, right), expected);
}

TEST(SeamFinderPipeline, InvalidSettingThrows) {
    cv::Mat left, right;
    make_overlaps(left, right);

    SeamFinderClass finder;
    finder.parameters.treatment = "cubic";
    EXPECT_THROW(finder.computeSeam(left, right), std::invalid_argument);
}

TEST(SeamFinderPipeline, DijkstraMethodsFollowTheMatchingBand) {
    cv::Mat left, right;
    make_overlaps(left, right);

    for (SolverMethod m : { SolverMethod::Dijkstra8, SolverMethod::DynamicProgrammingDijkstraLocal }) {
        SeamFinderClass finder(CostFunction::ColorGradient, CostMapParameters(), m);
        const std::vector<cv::Point> seam = finder.computeSeam(left, right);
        ASSERT_FALSE(seam.empty()) << getMethodName(m);
        EXPECT_EQ(seam.front().y, 0) << getMethodName(m);
        EXPECT_EQ(seam.back().y, left.rows - 1) << getMethodName(m);
        for (const cv::Point& p : seam) {
            EXPECT_GE(p.x, 18) << getMethodName(m);
            EXPECT_LE(p.x, 29) << getMethodName(m);
        }
        const cv::Mat cost = computeCost(left, right, CostFunction::ColorGradient, CostMapParameters()).treated;
        EXPECT_EQ(seam, solveEdge(cost, m, nullptr, nullptr, finder.corridorRadius).path) << getMethodName(m);
    }
}

TEST(SeamFinderPipeline, CorridorRadiusIsUsed) {
    cv::Mat left, right;
    make_overlaps(left, right);

    SeamFinderClass finder(CostFunction::ColorGradient, CostMapParameters(), SolverMethod::DynamicProgrammingDijkstraLocal);
    EXPECT_EQ(finder.method, SolverMethod::DynamicProgrammingDijkstraLocal);
    EXPECT_EQ(finder.corridorRadius, 18);
    finder.corridorRadius = 0;
    const cv::Mat cost = computeCost(left, right, CostFunction::ColorGradient, CostMapParameters()).treated;
    EXPECT_EQ(finder.computeSeam(left, right), solveEdge(cost, SolverMethod::DynamicProgrammingDijkstraLocal, nullptr, nullptr, 0).path);
    EXPECT_EQ(SeamFinderClass().method, SolverMethod::DynamicProgramming);
}
