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

struct PredictionResult {
    std::string className;
    double confidence = 0.0;
};

class ImageRecognizer {
public:
    explicit ImageRecognizer(const std::string& model_path = "",
                             const std::string& label_path = "");

    PredictionResult PredictFromFile(const std::string& image_path);
    PredictionResult PredictFromBuffer(const std::vector<unsigned char>& image_data);

#ifdef CPPAI_WITH_VISION
    PredictionResult PredictFromMat(const cv::Mat& img);
#endif

private:
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
    PredictionResult PredictWithDashScope(const std::vector<unsigned char>& image_data);
#endif
};
