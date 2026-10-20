#pragma once
#include <opencv2/core.hpp>

namespace cv::dnn {

inline Mat blobFromImage(const Mat& image, double scalefactor = 1.0, const Size& = Size(), const Scalar& = Scalar(), bool = false, bool = false) {
    Mat blob;
    blob.dims = 4;
    blob.size[0] = 1;
    blob.size[1] = image.channels();
    blob.size[2] = image.rows;
    blob.size[3] = image.cols;
    blob._type = CV_32F;
    blob.data_float.resize(image.rows * image.cols * image.channels());
    for (size_t i = 0; i < image.data_float.size(); ++i) {
        blob.data_float[i] = image.data_float[i] * static_cast<float>(scalefactor);
    }
    return blob;
}

} // namespace cv::dnn
