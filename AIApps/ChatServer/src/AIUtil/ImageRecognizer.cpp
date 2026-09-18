#include "../include/AIUtil/ImageRecognizer.h"
#include "../include/AIUtil/EnvUtil.h"
#include "../include/AIUtil/base64.h"
#include "../../../../HttpServer/include/utils/JsonUtil.h"

#include <stdexcept>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <curl/curl.h>

namespace {

size_t onWrite(void* buffer, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(buffer), size * nmemb);
    return size * nmemb;
}

std::string detectMime(const std::vector<unsigned char>& data) {
    if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return "image/jpeg";
    }
    if (data.size() >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return "image/png";
    }
    if (data.size() >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F') {
        return "image/gif";
    }
    if (data.size() >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F'
        && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
        return "image/webp";
    }
    return "image/jpeg";
}

std::string extractJsonObject(const std::string& text) {
    const auto start = text.find('{');
    const auto end = text.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return {};
    }
    return text.substr(start, end - start + 1);
}

}  // namespace

#ifdef CPPAI_WITH_VISION

ImageRecognizer::ImageRecognizer(const std::string& model_path,
                                 const std::string& label_path)
    : env(ORT_LOGGING_LEVEL_WARNING, "ImageRecognizer")
{
    const std::string model = model_path.empty()
        ? envOr("VISION_ONNX_PATH", "")
        : model_path;
    const std::string labelsFile = label_path.empty()
        ? envOr("VISION_LABELS_PATH", "")
        : label_path;
    if (model.empty() || labelsFile.empty()) {
        throw std::runtime_error("VISION_ONNX_PATH / VISION_LABELS_PATH is not set");
    }

    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(1);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

    session = std::make_unique<Ort::Session>(env, model.c_str(), session_options);
    allocator = std::make_unique<Ort::AllocatorWithDefaultOptions>();

    input_name = session->GetInputNameAllocated(0, *allocator).get();
    output_name = session->GetOutputNameAllocated(0, *allocator).get();
    input_shape = session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    input_height = static_cast<int>(input_shape[2]);
    input_width = static_cast<int>(input_shape[3]);
    LoadLabels(labelsFile);
}

void ImageRecognizer::LoadLabels(const std::string& label_path) {
    std::ifstream infile(label_path);
    if (!infile.is_open()) {
        throw std::runtime_error("Failed to open label file: " + label_path);
    }
    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty()) {
            labels.push_back(line);
        }
    }
    if (labels.empty()) {
        throw std::runtime_error("No labels loaded from file: " + label_path);
    }
}

std::string ImageRecognizer::PredictFromFile(const std::string& image_path) {
    cv::Mat img = cv::imread(image_path);
    if (img.empty()) {
        throw std::runtime_error("Failed to load image: " + image_path);
    }
    return PredictFromMat(img);
}

std::string ImageRecognizer::PredictFromBuffer(const std::vector<unsigned char>& image_data) {
    cv::Mat img = cv::imdecode(image_data, cv::IMREAD_COLOR);
    if (img.empty()) {
        throw std::runtime_error("Failed to decode image from buffer");
    }
    return PredictFromMat(img);
}

std::string ImageRecognizer::PredictFromMat(const cv::Mat& img_raw) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (img_raw.empty()) {
        throw std::runtime_error("Input image is empty");
    }

    cv::Mat img;
    cv::resize(img_raw, img, cv::Size(input_width, input_height));
    img.convertTo(img, CV_32F, 1.0 / 255.0);
    cv::dnn::blobFromImage(img, img);

    std::vector<int64_t> dims = { 1, 3, input_height, input_width };
    size_t input_tensor_size = 1 * 3 * static_cast<size_t>(input_height) * static_cast<size_t>(input_width);

    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
        OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, img.ptr<float>(), input_tensor_size, dims.data(), dims.size());

    const char* input_names[] = { input_name.c_str() };
    const char* output_names[] = { output_name.c_str() };
    auto output_tensors = session->Run(
        Ort::RunOptions{ nullptr },
        input_names, &input_tensor, 1,
        output_names, 1);

    float* output_data = output_tensors.front().GetTensorMutableData<float>();
    int num_classes = labels.empty() ? 1000 : static_cast<int>(labels.size());
    int pred_class = static_cast<int>(std::max_element(output_data, output_data + num_classes) - output_data);

    double sumExp = 0.0;
    for (int i = 0; i < num_classes; ++i) {
        sumExp += std::exp(static_cast<double>(output_data[i]));
    }
    last_confidence_ = sumExp > 0.0
        ? std::exp(static_cast<double>(output_data[pred_class])) / sumExp
        : 0.0;

    if (pred_class >= 0 && pred_class < static_cast<int>(labels.size())) {
        return labels[pred_class];
    }
    return "Unknown";
}

#else

ImageRecognizer::ImageRecognizer(const std::string&, const std::string&) {}

std::string ImageRecognizer::PredictFromFile(const std::string& image_path) {
    std::ifstream in(image_path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to load image: " + image_path);
    }
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    return PredictFromBuffer(data);
}

std::string ImageRecognizer::PredictFromBuffer(const std::vector<unsigned char>& image_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    return PredictWithDashScope(image_data);
}

std::string ImageRecognizer::PredictWithDashScope(const std::vector<unsigned char>& image_data) {
    if (image_data.empty()) {
        throw std::runtime_error("图片为空");
    }
    const char* key = std::getenv("DASHSCOPE_API_KEY");
    if (!key || !*key) {
        throw std::runtime_error("未配置 DASHSCOPE_API_KEY，无法进行图像识别");
    }

    const std::string mime = detectMime(image_data);
    const std::string b64 = base64_encode(image_data.data(), image_data.size());
    const std::string dataUrl = "data:" + mime + ";base64," + b64;
    const std::string model = envOr("DASHSCOPE_VL_MODEL", "qwen3.8-flash");

    json payload;
    payload["model"] = model;
    payload["enable_thinking"] = false;
    payload["messages"] = json::array({
        {
            {"role", "user"},
            {"content", json::array({
                {
                    {"type", "image_url"},
                    {"image_url", {{"url", dataUrl}}}
                },
                {
                    {"type", "text"},
                    {"text", "识别图片中的主要物体或场景。"
                             "只输出 JSON：{\"class_name\":\"简短中文名称\",\"confidence\":0到1的小数}。"
                             "不要输出其他文字。"}
                }
            })}
        }
    });
    const std::string body = payload.dump();

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }

    std::string response;
    struct curl_slist* headers = nullptr;
    const std::string auth = "Authorization: Bearer " + std::string(key);
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,
                     "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const CURLcode rc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        throw std::runtime_error(std::string("图像识别请求失败: ") + curl_easy_strerror(rc));
    }

    json root;
    try {
        root = json::parse(response);
    } catch (...) {
        throw std::runtime_error("图像识别返回无法解析");
    }

    if (root.contains("error")) {
        const auto& err = root["error"];
        std::string message = "图像识别失败";
        if (err.is_object() && err.contains("message") && err["message"].is_string()) {
            message = err["message"].get<std::string>();
            if (err.contains("code") && err["code"].is_string()) {
                message = err["code"].get<std::string>() + ": " + message;
            }
        } else if (err.is_string()) {
            message = err.get<std::string>();
        }
        throw std::runtime_error(message);
    }
    if (httpCode >= 400) {
        throw std::runtime_error("图像识别 HTTP " + std::to_string(httpCode));
    }

    std::string content;
    if (root.contains("choices") && root["choices"].is_array() && !root["choices"].empty()) {
        const auto& message = root["choices"][0]["message"];
        if (message.contains("content") && message["content"].is_string()) {
            content = message["content"].get<std::string>();
        }
    }
    if (content.empty()) {
        throw std::runtime_error("图像识别未返回结果");
    }

    last_confidence_ = 0.85;
    const std::string jsonText = extractJsonObject(content);
    if (!jsonText.empty()) {
        try {
            json parsed = json::parse(jsonText);
            if (parsed.contains("confidence") && parsed["confidence"].is_number()) {
                last_confidence_ = parsed["confidence"].get<double>();
                if (last_confidence_ < 0.0) last_confidence_ = 0.0;
                if (last_confidence_ > 1.0) last_confidence_ = 1.0;
            }
            if (parsed.contains("class_name") && parsed["class_name"].is_string()) {
                const std::string name = parsed["class_name"].get<std::string>();
                if (!name.empty()) {
                    return name;
                }
            }
        } catch (...) {
        }
    }
    return content;
}

#endif
