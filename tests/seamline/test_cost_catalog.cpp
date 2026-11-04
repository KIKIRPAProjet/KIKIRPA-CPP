#include "algorithms/seamline/dynamic_programming_dijkstra/cost_catalog.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace DynamicProgrammingDijkstraNS;

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
    for (CostFunction fn : all_cost_functions())
        for (const char* n : { "noise", "percentile" })
            for (const char* c : { "manhattan", "euclidian" })
                for (const char* t : { "sigmoid", "linear", "squared", "caped" })
                    for (bool r : {false, true}) {
                        CostMapParams p;
                        p.normalization = n;
                        p.color_norm = c;
                        p.treatment = t;
                        p.recall = r;
                        const CostMaps m = compute_cost(a, b, fn, p);
                        EXPECT_EQ(m.treated.type(), CV_32F);
                        EXPECT_EQ(m.treated.size(), a.size());
                        EXPECT_TRUE(cv::checkRange(m.treated)) << cost_function_name(fn) << " " << n << " " << c;
                    }
}

TEST(CostCatalog, ExampleNoiseEuclidianSigmoidRecall) {
    cv::Mat a, b;
    random_pair(a, b);
    CostMapParams p;
    p.normalization = "noise";
    p.color_norm = "euclidian";
    p.treatment = "sigmoid";
    p.recall = true;
    const CostMaps m = compute_cost(a, b, CostFunction::ColorGradient, p);
    EXPECT_EQ(max_abs_diff(m.treated, compute_cost_maps(a, b, p).treated), 0.0);
}

TEST(CostCatalog, ColorOnlyAndGradientOnlyFixTheWeight) {
    cv::Mat a, b;
    random_pair(a, b);
    CostMapParams p;
    p.w_color = 0.3;   // doit être ignoré
    CostMapParams p1 = p, p0 = p;
    p1.w_color = 1.0;
    p0.w_color = 0.0;
    EXPECT_EQ(max_abs_diff(compute_cost(a, b, CostFunction::ColorOnly, p).treated, compute_cost_maps(a, b, p1).treated), 0.0);
    EXPECT_EQ(max_abs_diff(compute_cost(a, b, CostFunction::GradientOnly, p).treated, compute_cost_maps(a, b, p0).treated), 0.0);
}

TEST(CostCatalog, CenterPriorIsZeroAtCenterAndWeightAtBorders) {
    cv::Mat a, b;
    random_pair(a, b, 10, 21);
    CostMapParams p;
    p.treatment = "linear";
    const cv::Mat base = compute_cost_maps(a, b, p).treated;
    const cv::Mat with = compute_cost(a, b, CostFunction::CenterPrior, p).treated;
    EXPECT_NEAR(with.at<float>(3, 10) - base.at<float>(3, 10), 0.0, 1e-5);
    EXPECT_NEAR(with.at<float>(3, 0) - base.at<float>(3, 0), 0.15, 1e-5);
    EXPECT_NEAR(with.at<float>(3, 20) - base.at<float>(3, 20), 0.15, 1e-5);
}

TEST(CostCatalog, HighlightPenaltyAddsWeightOnSaturatedPixels) {
    cv::Mat a(10, 10, CV_8UC3, cv::Scalar(100, 100, 100)), b = a.clone();
    b.at<cv::Vec3b>(4, 4) = cv::Vec3b(255, 255, 255);   // reflet saturé dans B
    CostMapParams p;
    p.treatment = "linear";
    const cv::Mat base = compute_cost_maps(a, b, p).treated;
    const cv::Mat with = compute_cost(a, b, CostFunction::HighlightPenalty, p).treated;
    EXPECT_NEAR(with.at<float>(4, 4) - base.at<float>(4, 4), 1.0, 1e-5);
    EXPECT_NEAR(with.at<float>(0, 0) - base.at<float>(0, 0), 0.0, 1e-7);
}

TEST(CostCatalog, LocalNormIgnoresTreatmentAndStaysInUnitInterval) {
    cv::Mat a, b;
    random_pair(a, b);
    CostMapParams s, l;
    s.treatment = "sigmoid";
    l.treatment = "linear";
    const cv::Mat ms = compute_cost(a, b, CostFunction::LocalNorm, s).treated;
    EXPECT_EQ(max_abs_diff(ms, compute_cost(a, b, CostFunction::LocalNorm, l).treated), 0.0);
    double lo = 0.0, hi = 0.0;
    cv::minMaxLoc(ms, &lo, &hi);
    EXPECT_GE(lo, 0.0);
    EXPECT_LE(hi, 1.0 + 1e-6);
}

TEST(CostCatalog, NamesMatchTheNotebook) {
    EXPECT_EQ(all_cost_functions().size(), 7u);
    for (CostFunction fn : all_cost_functions()) EXPECT_EQ(cost_function_from_name(cost_function_name(fn)), fn);
    EXPECT_EQ(cost_function_from_name("local_norm"), CostFunction::LocalNorm);
    EXPECT_THROW(cost_function_from_name("gradient_diff"), std::invalid_argument);
}
