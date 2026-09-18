#pragma once
#include <string>
#include <vector>
#include <memory>
#include <mutex>

#ifdef CPPAI_WITH_VISION
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <onnxruntime_cxx_api.h>
#endif

class ImageRecognizer {
public:
    explicit ImageRecognizer(const std::string& model_path = "",
                             const std::string& label_path = "");

    std::string PredictFromFile(const std::string& image_path);
    std::string PredictFromBuffer(const std::vector<unsigned char>& image_data);
    double lastConfidence() const { return last_confidence_; }

#ifdef CPPAI_WITH_VISION
    std::string PredictFromMat(const cv::Mat& img);
#endif

private:
    double last_confidence_ = 0.0;
    std::mutex mutex_;

#ifdef CPPAI_WITH_VISION
    Ort::Env env;
    std::unique_ptr<Ort::Session> session;
    std::unique_ptr<Ort::AllocatorWithDefaultOptions> allocator;

    std::string input_name;
    std::string output_name;
    std::vector<int64_t> input_shape;
    int input_height{};
    int input_width{};
    std::vector<std::string> labels;

    void LoadLabels(const std::string& label_path);
#else
    std::string PredictWithDashScope(const std::vector<unsigned char>& image_data);
#endif
};
