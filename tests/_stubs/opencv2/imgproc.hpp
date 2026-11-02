#pragma once
#include <opencv2/core.hpp>

namespace cv {

enum ColorConversionCodes {
    COLOR_BGR2GRAY = 6,
    COLOR_BGRA2GRAY = 10,
    COLOR_GRAY2BGR = 8,
    COLOR_BGRA2BGR = 4
};

enum InterpolationFlags {
    INTER_AREA = 3
};

inline void cvtColor(const Mat& src, Mat& dst, int) {
    dst = Mat(src.rows, src.cols, CV_8U);
}

inline void resize(const Mat& src, Mat& dst, Size dsize, double = 0, double = 0, int = 0) {
    dst = Mat(dsize.height, dsize.width, src.type());
}

class CLAHE {
public:
    virtual ~CLAHE() = default;
    virtual void setClipLimit(double) = 0;
    virtual void apply(const Mat& src, Mat& dst) = 0;
};

class DummyCLAHE : public CLAHE {
public:
    void setClipLimit(double) override {}
    void apply(const Mat& src, Mat& dst) override {
        dst = src;
    }
};

inline std::shared_ptr<CLAHE> createCLAHE(double = 40.0, Size = Size(8,8)) {
    return std::make_shared<DummyCLAHE>();
}

} // namespace cv
