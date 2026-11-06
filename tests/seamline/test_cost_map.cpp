#include "algorithms/seamline/dynamic_programming_dijkstra/cost_map.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

using namespace DynamicProgrammingDijkstraNs;

TEST(CostMap, GradientOfConstantImageIsZero) {
    const cv::Mat img(20, 20, CV_32F, cv::Scalar(7.0));
    const Gradients g = computeGradients(img);
    EXPECT_EQ(g.gradientX.type(), CV_64F);
    EXPECT_EQ(cv::countNonZero(g.magnitude), 0);
}

TEST(CostMap, SobelOfHorizontalRampIsEight) {
    cv::Mat img(10, 10, CV_32F);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) img.at<float>(y, x) = float(x);   // pente de 1 par pixel
    const Gradients g = computeGradients(img);
    // Noyau de Sobel non normalisé : 1 + 2 + 1 lignes × écart de 2 pixels = 8, à l'intérieur de l'image.
    EXPECT_DOUBLE_EQ(g.gradientX.at<double>(5, 5), 8.0);
    EXPECT_DOUBLE_EQ(g.gradientY.at<double>(5, 5), 0.0);
    EXPECT_DOUBLE_EQ(g.magnitude.at<double>(5, 5), 8.0);
}

TEST(CostMap, MinmaxMapsToUnitIntervalAndConstantToZero) {
    const cv::Mat x = (cv::Mat_<float>(1, 3) << 2.f, 4.f, 6.f);
    const cv::Mat n = normalizeMinMax(x);
    EXPECT_FLOAT_EQ(n.at<float>(0, 0), 0.f);
    EXPECT_FLOAT_EQ(n.at<float>(0, 1), 0.5f);
    EXPECT_FLOAT_EQ(n.at<float>(0, 2), 1.f);
    EXPECT_EQ(cv::countNonZero(normalizeMinMax(cv::Mat(3, 3, CV_32F, cv::Scalar(5.0)))), 0);
}

TEST(CostMap, MadIsRobustToOutlier) {
    // médiane = 3 ; écarts = 2, 1, 0, 1, 97 ; médiane des écarts = 1
    const cv::Mat x = (cv::Mat_<double>(1, 5) << 1, 2, 3, 4, 100);
    EXPECT_DOUBLE_EQ(computeMedianAbsoluteDeviation(x), 1.0);
}

TEST(CostMap, PercentileDividesByNthPercentileAndCaps) {
    cv::Mat x(1, 101, CV_32F);
    for (int i = 0; i <= 100; ++i) x.at<float>(0, i) = float(i);   // 99e percentile = 99
    const cv::Mat p = normalizeByPercentile(x, 99);
    EXPECT_NEAR(p.at<float>(0, 50), 50.0 / 99.0, 1e-6);
    EXPECT_FLOAT_EQ(p.at<float>(0, 100), 1.f);                    // 100 est plafonné à 99
}

TEST(CostMap, GrayRawKeepsSignAndUnits) {
    const cv::Mat img(4, 4, CV_32FC3, cv::Scalar(-1000.0, -1000.0, -1000.0));
    const cv::Mat g = convertToRawGrey(img);
    EXPECT_EQ(g.type(), CV_32F);
    EXPECT_NEAR(g.at<float>(0, 0), -1000.f, 1e-2);
}

TEST(CostMap, IdenticalOverlapsGiveFlatSigmoidCost) {
    cv::Mat a(40, 12, CV_16UC3);
    cv::randu(a, cv::Scalar::all(0), cv::Scalar::all(65535));
    const CostMaps m = computeCostMaps(a, a.clone());
    EXPECT_EQ(m.colorTerm.type(), CV_32F);
    EXPECT_EQ(m.gradientTerm.type(), CV_64F);
    EXPECT_EQ(m.treated.type(), CV_32F);
    EXPECT_EQ(m.treated.size(), a.size());
    // A = B : combined = 0, donc sigmoïde = 1 / (1 + e^12) partout.
    double lo = 0.0, hi = 0.0;
    cv::minMaxLoc(m.treated, &lo, &hi);
    EXPECT_NEAR(lo, 1.0 / (1.0 + std::exp(12.0)), 1e-9);
    EXPECT_NEAR(hi, 1.0 / (1.0 + std::exp(12.0)), 1e-9);
}

TEST(CostMap, RecallIsZeroAtCenterAndLamAtBorder) {
    cv::Mat a(10, 21, CV_8UC3, cv::Scalar(100, 100, 100));
    CostMapParameters p;
    p.treatment = "linear";
    p.recall = true;
    p.recallWeight = 0.05;
    const CostMaps m = computeCostMaps(a, a.clone(), p);   // coût nul hors rappel
    EXPECT_NEAR(m.treated.at<float>(5, 10), 0.0, 1e-7);      // centre
    EXPECT_NEAR(m.treated.at<float>(5, 0), 0.05, 1e-7);      // bord gauche
    EXPECT_NEAR(m.treated.at<float>(5, 20), 0.05, 1e-7);     // bord droit
}

TEST(CostMap, AllOptionsRunAndInvalidOptionsThrow) {
    cv::Mat a(30, 15, CV_8UC3), b(30, 15, CV_8UC3);
    cv::randu(a, cv::Scalar::all(0), cv::Scalar::all(255));
    cv::randu(b, cv::Scalar::all(0), cv::Scalar::all(255));
    for (const char* n : { "noise", "percentile" })
        for (const char* c : { "manhattan", "euclidian" })
            for (const char* t : { "sigmoid", "linear", "squared", "caped" }) {
                CostMapParameters p;
                p.normalization = n;
                p.colorNorm = c;
                p.treatment = t;
                const CostMaps m = computeCostMaps(a, b, p);
                EXPECT_TRUE(cv::checkRange(m.treated)) << n << " " << c << " " << t;
            }
    CostMapParameters bad;
    bad.treatment = "cliped";
    EXPECT_THROW(computeCostMaps(a, b, bad), std::invalid_argument);
}

// La sigmoïde (tau = 3) est calibrée pour des unités de bruit : elle n'est appliquée
// qu'avec normalization = "noise". Avec "percentile", les valeurs sont dans [0, 1] et
// treatment = "sigmoid" se comporte comme "linear".
TEST(CostMap, SigmoidOnlyAppliesToNoiseNormalization) {
    cv::Mat a(30, 20, CV_8UC3), b(30, 20, CV_8UC3);
    cv::randu(a, cv::Scalar::all(0), cv::Scalar::all(255));
    cv::randu(b, cv::Scalar::all(0), cv::Scalar::all(255));

    CostMapParameters sig, lin;
    lin.treatment = "linear";

    sig.normalization = lin.normalization = "percentile";
    EXPECT_EQ(cv::norm(computeCostMaps(a, b, sig).treated, computeCostMaps(a, b, lin).treated, cv::NORM_INF), 0.0);

    sig.normalization = lin.normalization = "noise";
    const cv::Mat t = computeCostMaps(a, b, sig).treated;
    EXPECT_GT(cv::norm(t, computeCostMaps(a, b, lin).treated, cv::NORM_INF), 0.0);
    double lo = 0.0, hi = 0.0;
    cv::minMaxLoc(t, &lo, &hi);
    EXPECT_GE(lo, 0.0);
    EXPECT_LE(hi, 1.0);
}
