// tests/frontend/test_frontend_utils.cpp
#include "frontendutils.h"

#include <gtest/gtest.h>

namespace {

TEST(FrontendUtilsTestClass, ZoomInStepProgression) {
    EXPECT_EQ(FrontendUtils::nextZoomIn(1), 2);
    EXPECT_EQ(FrontendUtils::nextZoomIn(2), 5);
    EXPECT_EQ(FrontendUtils::nextZoomIn(75), 100);
    EXPECT_EQ(FrontendUtils::nextZoomIn(100), 125);
    EXPECT_EQ(FrontendUtils::nextZoomIn(1400), 1600);
    EXPECT_EQ(FrontendUtils::nextZoomIn(1600), 1600);
}

TEST(FrontendUtilsTestClass, ZoomOutStepProgression) {
    EXPECT_EQ(FrontendUtils::nextZoomOut(1600), 1400);
    EXPECT_EQ(FrontendUtils::nextZoomOut(125), 100);
    EXPECT_EQ(FrontendUtils::nextZoomOut(100), 75);
    EXPECT_EQ(FrontendUtils::nextZoomOut(2), 1);
    EXPECT_EQ(FrontendUtils::nextZoomOut(1), 1);
}

} // namespace
