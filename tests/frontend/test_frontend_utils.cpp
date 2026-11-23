// tests/frontend/test_frontend_utils.cpp
#include "frontendutils.h"

#include <gtest/gtest.h>

namespace {

constexpr double ZOOM_FULL = 100.0;
constexpr double ZOOM_TWELVE = 12.0;
constexpr double ZOOM_TWELVE_POINT_FOUR = 12.4;
constexpr double ZOOM_TWO = 2.0;
constexpr double ZOOM_QUARTER = 0.25;
constexpr double ZOOM_TWO_TENTHS = 0.2;
constexpr double ZOOM_ONE_POINT_SIX = 1.6;

TEST(FrontendUtilsTestClass, FormatsZoomWithDecimalsAndIntegers) {
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_FULL).toStdString(), "100 %");
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_TWELVE).toStdString(), "12 %");
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_TWELVE_POINT_FOUR).toStdString(), "12,4 %");
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_TWO).toStdString(), "2 %");
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_QUARTER).toStdString(), "0,25 %");
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_TWO_TENTHS).toStdString(), "0,2 %");
  EXPECT_EQ(FrontendUtils::formatZoom(ZOOM_ONE_POINT_SIX).toStdString(), "1,6 %");
}

} // namespace
