#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <memory>

#define CV_8U 0
#define CV_16U 2
#define CV_32F 5
#define CV_8UC1 0
#define CV_8UC3 16
#define CV_8UC4 24
#define CV_16UC1 2
#define CV_16UC3 18
#define CV_16UC4 26
#define CV_MAKETYPE(depth, cn) ((depth) + (((cn)-1) << 3))

using uchar = unsigned char;

namespace cv {

template<typename T>
struct Point_ {
    T x = 0, y = 0;
    Point_() = default;
    Point_(T _x, T _y) : x(_x), y(_y) {}
    bool operator==(const Point_& o) const { return std::abs(x - o.x) < 1e-4 && std::abs(y - o.y) < 1e-4; }
};

template<typename T>
inline Point_<T> operator+(const Point_<T>& a, const Point_<T>& b) {
    return Point_<T>(a.x + b.x, a.y + b.y);
}

using Point2f = Point_<float>;
using Point2d = Point_<double>;
using Point = Point_<int>;

struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
    Rect() = default;
    Rect(int _x, int _y, int _w, int _h) : x(_x), y(_y), width(_w), height(_h) {}
};

struct Size {
    int width = 0, height = 0;
    Size() = default;
    Size(int w, int h) : width(w), height(h) {}
};

struct Scalar {
    double val[4] = {0,0,0,0};
    Scalar() = default;
    Scalar(double v0, double v1=0, double v2=0, double v3=0) { val[0]=v0; val[1]=v1; val[2]=v2; val[3]=v3; }
};

struct KeyPoint {
    Point2f pt;
    KeyPoint() = default;
    KeyPoint(Point2f p, float) : pt(p) {}
    KeyPoint(float x, float y, float) : pt(x, y) {}
};

struct DMatch {
    int queryIdx = 0;
    int trainIdx = 0;
    float distance = 0.f;
    DMatch() = default;
    DMatch(int q, int t, float d) : queryIdx(q), trainIdx(t), distance(d) {}
};

class Mat {
public:
    int rows = 0;
    int cols = 0;
    int _type = CV_8U;
    int dims = 2;
    int size[4] = {0, 0, 0, 0};
    std::vector<float> data_float;
    std::vector<uint16_t> data_u16;
    std::vector<uint8_t> data_u8;
    int x_off = 0, y_off = 0;

    Mat() = default;
    Mat(int r, int c, int t) : rows(r), cols(c), _type(t) {
        allocate();
    }
    Mat(int r, int c, int t, const Scalar&) : rows(r), cols(c), _type(t) {
        allocate();
    }
    void allocate() {
        if (_type == CV_32F) data_float.assign(rows * cols, 0.f);
        else if (_type == CV_16U) data_u16.assign(rows * cols, 0);
        else data_u8.assign(rows * cols, 0);
    }
    bool empty() const { return rows == 0 || cols == 0; }
    int type() const { return _type; }
    int depth() const { return _type & 7; }
    int channels() const { return (_type >> 3) + 1; }
    size_t total() const {
        if (dims > 2) {
            size_t t = 1;
            for (int i = 0; i < dims; ++i) t *= size[i];
            return t;
        }
        return rows * cols;
    }

    Mat operator()(const Rect& roi) const {
        Mat sub(roi.height, roi.width, _type);
        sub.x_off = x_off + roi.x;
        sub.y_off = y_off + roi.y;
        for (int y = 0; y < roi.height; ++y) {
            for (int x = 0; x < roi.width; ++x) {
                int srcY = roi.y + y;
                int srcX = roi.x + x;
                if (_type == CV_16U) {
                    sub.data_u16[y * roi.width + x] = data_u16[srcY * cols + srcX];
                } else if (_type == CV_32F) {
                    sub.data_float[y * roi.width + x] = data_float[srcY * cols + srcX];
                } else {
                    sub.data_u8[y * roi.width + x] = data_u8[srcY * cols + srcX];
                }
            }
        }
        return sub;
    }

    void convertTo(Mat& dst, int rtype, double alpha = 1.0, double beta = 0.0) const {
        Mat tmp(rows, cols, rtype);
        tmp.x_off = x_off;
        tmp.y_off = y_off;
        for (int i = 0; i < rows * cols; ++i) {
            double v = 0.0;
            if (_type == CV_16U) v = data_u16[i];
            else if (_type == CV_32F) v = data_float[i];
            else v = data_u8[i];
            v = v * alpha + beta;
            if (rtype == CV_8U) {
                tmp.data_u8[i] = static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
            } else if (rtype == CV_16U) {
                tmp.data_u16[i] = static_cast<uint16_t>(std::clamp(v, 0.0, 65535.0));
            } else if (rtype == CV_32F) {
                tmp.data_float[i] = static_cast<float>(v);
            }
        }
        dst = std::move(tmp);
    }

    template<typename T = float>
    T* ptr(int = 0) { 
        if (_type == CV_32F) return reinterpret_cast<T*>(data_float.data());
        if (_type == CV_16U) return reinterpret_cast<T*>(data_u16.data());
        return reinterpret_cast<T*>(data_u8.data());
    }
    template<typename T = float>
    const T* ptr(int = 0) const {
        if (_type == CV_32F) return reinterpret_cast<const T*>(data_float.data());
        if (_type == CV_16U) return reinterpret_cast<const T*>(data_u16.data());
        return reinterpret_cast<const T*>(data_u8.data());
    }
};

inline void minMaxLoc(const Mat& src, double* minVal, double* maxVal, Point* = nullptr, Point* = nullptr) {
    if (src.total() == 0) { if (minVal) *minVal = 0; if (maxVal) *maxVal = 0; return; }
    double mn = 1e18, mx = -1e18;
    for (size_t i = 0; i < src.total(); ++i) {
        double v = 0;
        if (src.type() == CV_16U) v = src.data_u16[i];
        else if (src.type() == CV_32F) v = src.data_float[i];
        else v = src.data_u8[i];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    if (minVal) *minVal = mn;
    if (maxVal) *maxVal = mx;
}

inline Mat noArray() { return Mat(); }

} // namespace cv
