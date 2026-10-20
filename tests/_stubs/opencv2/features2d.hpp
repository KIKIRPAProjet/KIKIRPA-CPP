#pragma once
#include <opencv2/core.hpp>
#include <memory>
#include <cmath>

namespace cv {

class Feature2D {
public:
    virtual ~Feature2D() = default;
    virtual void detectAndCompute(const Mat& image, const Mat& mask, std::vector<KeyPoint>& keypoints, Mat& descriptors) = 0;
};

class SIFT : public Feature2D {
public:
    static std::shared_ptr<SIFT> create(int = 0, int = 3, double = 0.04, double = 10, double = 1.6);
};

constexpr int NORM_L2 = 4;

class DescriptorMatcher {
public:
    virtual ~DescriptorMatcher() = default;
    virtual void knnMatch(const Mat& queryDescriptors, const Mat& trainDescriptors, std::vector<std::vector<DMatch>>& matches, int k) = 0;
    static std::shared_ptr<DescriptorMatcher> create(const std::string& type);
};

class BFMatcher : public DescriptorMatcher {
public:
    BFMatcher(int = 0, bool = false) {}
    void knnMatch(const Mat& queryDescriptors, const Mat& trainDescriptors, std::vector<std::vector<DMatch>>& matches, int k) override;
};

class StubSIFT : public SIFT {
public:
    void detectAndCompute(const Mat& image, const Mat&, std::vector<KeyPoint>& keypoints, Mat& descriptors) override {
        keypoints.clear();
        for (int y = 5; y < image.rows - 5; ++y) {
            for (int x = 5; x < image.cols - 5; ++x) {
                uint8_t val = (image.depth() == CV_16U) ? (image.data_u16[y * image.cols + x] > 1000 ? 255 : 0) : image.data_u8[y * image.cols + x];
                if (val > 150) {
                    keypoints.emplace_back(static_cast<float>(x), static_cast<float>(y), 1.f);
                }
            }
        }
        descriptors = Mat(static_cast<int>(keypoints.size()), 2, CV_32F);
        for (size_t i = 0; i < keypoints.size(); ++i) {
            descriptors.data_float[i * 2 + 0] = keypoints[i].pt.x + image.x_off;
            descriptors.data_float[i * 2 + 1] = keypoints[i].pt.y + image.y_off;
        }
    }
};

inline std::shared_ptr<SIFT> SIFT::create(int, int, double, double, double) {
    return std::make_shared<StubSIFT>();
}

class StubMatcher : public DescriptorMatcher {
public:
    void knnMatch(const Mat& qDesc, const Mat& tDesc, std::vector<std::vector<DMatch>>& matches, int k) override {
        matches.clear();
        for (int i = 0; i < qDesc.rows; ++i) {
            float qx = qDesc.data_float[i * 2 + 0];
            float qy = qDesc.data_float[i * 2 + 1];
            std::vector<std::pair<float, int>> dists;
            for (int j = 0; j < tDesc.rows; ++j) {
                float tx = tDesc.data_float[j * 2 + 0];
                float ty = tDesc.data_float[j * 2 + 1];
                float d = std::sqrt((qx - tx) * (qx - tx) + (qy - ty) * (qy - ty));
                dists.emplace_back(d, j);
            }
            std::sort(dists.begin(), dists.end());
            std::vector<DMatch> row;
            for (int m = 0; m < k && m < static_cast<int>(dists.size()); ++m) {
                row.emplace_back(i, dists[m].second, dists[m].first);
            }
            matches.push_back(row);
        }
    }
};

inline void BFMatcher::knnMatch(const Mat& qDesc, const Mat& tDesc, std::vector<std::vector<DMatch>>& matches, int k) {
    StubMatcher().knnMatch(qDesc, tDesc, matches, k);
}

inline std::shared_ptr<DescriptorMatcher> DescriptorMatcher::create(const std::string&) {
    return std::make_shared<StubMatcher>();
}

} // namespace cv
