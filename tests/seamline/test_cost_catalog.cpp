#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace DynamicProgrammingDijkstraNs;

namespace {

    void random_pair(cv::Mat& a, cv::Mat& b, int h = 40, int w = 21) {
        a.create(h, w, CV_16UC3);
        b.create(h, w, CV_16UC3);
        cv::randu(a, cv::Scalar::all(10000), cv::Scalar::all(50000));
        cv::randu(b, cv::Scalar::all(10000), cv::Scalar::all(50000));
    }

    double max_abs_diff(const cv::Mat& x, const cv::Mat& y) {
        return cv::norm(x, y, cv::NORM_INF);
    }

}  // namespace

TEST(CostCatalog, EveryFunctionRunsWithEveryMode) {
    cv::Mat a, b;
    random_pair(a, b);
    for (CostFunction fn : getAllCostFunctions())
        for (const char* n : { "noise", "percentile" })
            for (const char* c : { "manhattan", "euclidian" })
                for (const char* t : { "sigmoid", "linear", "squared", "caped" })
                    for (bool r : {false, true}) {
                        CostMapParameters p;
                        p.normalization = n;
                        p.colorNorm = c;
                        p.treatment = t;
                        p.recall = r;
                        const CostMaps m = computeCost(a, b, fn, p);
                        EXPECT_EQ(m.treated.type(), CV_32F);
                        EXPECT_EQ(m.treated.size(), a.size());
                        EXPECT_TRUE(cv::checkRange(m.treated)) << getCostFunctionName(fn) << " " << n << " " << c;
                    }
}

TEST(CostCatalog, ExampleNoiseEuclidianSigmoidRecall) {
    cv::Mat a, b;
    random_pair(a, b);
    CostMapParameters p;
    p.normalization = "noise";
    p.colorNorm = "euclidian";
    p.treatment = "sigmoid";
    p.recall = true;
    const CostMaps m = computeCost(a, b, CostFunction::ColorGradient, p);
    EXPECT_EQ(max_abs_diff(m.treated, computeCostMaps(a, b, p).treated), 0.0);
}

TEST(CostCatalog, ColorOnlyAndGradientOnlyFixTheWeight) {
    cv::Mat a, b;
    random_pair(a, b);
    CostMapParameters p;
    p.colorWeight = 0.3;   // doit être ignoré
    CostMapParameters p1 = p, p0 = p;
    p1.colorWeight = 1.0;
    p0.colorWeight = 0.0;
    EXPECT_EQ(max_abs_diff(computeCost(a, b, CostFunction::ColorOnly, p).treated, computeCostMaps(a, b, p1).treated), 0.0);
    EXPECT_EQ(max_abs_diff(computeCost(a, b, CostFunction::GradientOnly, p).treated, computeCostMaps(a, b, p0).treated), 0.0);
}

TEST(CostCatalog, CenterPriorIsZeroAtCenterAndWeightAtBorders) {
    cv::Mat a, b;
    random_pair(a, b, 10, 21);
    CostMapParameters p;
    p.treatment = "linear";
    const cv::Mat base = computeCostMaps(a, b, p).treated;
    const cv::Mat with = computeCost(a, b, CostFunction::CenterPrior, p).treated;
    EXPECT_NEAR(with.at<float>(3, 10) - base.at<float>(3, 10), 0.0, 1e-5);
    EXPECT_NEAR(with.at<float>(3, 0) - base.at<float>(3, 0), 0.15, 1e-5);
    EXPECT_NEAR(with.at<float>(3, 20) - base.at<float>(3, 20), 0.15, 1e-5);
}

TEST(CostCatalog, HighlightPenaltyAddsWeightOnSaturatedPixels) {
    cv::Mat a(10, 10, CV_8UC3, cv::Scalar(100, 100, 100)), b = a.clone();
    b.at<cv::Vec3b>(4, 4) = cv::Vec3b(255, 255, 255);   // reflet saturé dans B
    CostMapParameters p;
    p.treatment = "linear";
    const cv::Mat base = computeCostMaps(a, b, p).treated;
    const cv::Mat with = computeCost(a, b, CostFunction::HighlightPenalty, p).treated;
    EXPECT_NEAR(with.at<float>(4, 4) - base.at<float>(4, 4), 1.0, 1e-5);
    EXPECT_NEAR(with.at<float>(0, 0) - base.at<float>(0, 0), 0.0, 1e-7);
}

TEST(CostCatalog, LocalNormIgnoresTreatmentAndStaysInUnitInterval) {
    cv::Mat a, b;
    random_pair(a, b);
    CostMapParameters s, l;
    s.treatment = "sigmoid";
    l.treatment = "linear";
    const cv::Mat ms = computeCost(a, b, CostFunction::LocalNormalization, s).treated;
    EXPECT_EQ(max_abs_diff(ms, computeCost(a, b, CostFunction::LocalNormalization, l).treated), 0.0);
    double lo = 0.0, hi = 0.0;
    cv::minMaxLoc(ms, &lo, &hi);
    EXPECT_GE(lo, 0.0);
    EXPECT_LE(hi, 1.0 + 1e-6);
}

TEST(CostCatalog, NamesMatchTheNotebook) {
    EXPECT_EQ(getAllCostFunctions().size(), 7u);
    for (CostFunction fn : getAllCostFunctions()) EXPECT_EQ(parseCostFunctionName(getCostFunctionName(fn)), fn);
    EXPECT_EQ(parseCostFunctionName("local_norm"), CostFunction::LocalNormalization);
    EXPECT_THROW(parseCostFunctionName("gradient_diff"), std::invalid_argument);
}
