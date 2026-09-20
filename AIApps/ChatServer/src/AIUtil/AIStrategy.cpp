#include"../include/AIUtil/AIStrategy.h"
#include"../include/AIUtil/AIFactory.h"

namespace {

constexpr const char* kDashScopeChatModel = "qwen3.8-27b";

std::string dashScopeErrorMessage(const json& response) {
    if (response.contains("error")) {
        const auto& err = response["error"];
        if (err.is_string()) {
            return err.get<std::string>();
        }
        if (err.is_object()) {
            std::string code;
            std::string message;
            if (err.contains("code") && err["code"].is_string()) {
                code = err["code"].get<std::string>();
            }
            if (err.contains("message") && err["message"].is_string()) {
                message = err["message"].get<std::string>();
            }
            if (!code.empty() && !message.empty()) {
                return code + ": " + message;
            }
            return !message.empty() ? message : code;
        }
    }
    if (!response.contains("choices") && response.contains("message")
        && response["message"].is_string()) {
        return response["message"].get<std::string>();
    }
    return {};
}

std::string parseDashScopeContent(const json& response) {
    const std::string apiError = dashScopeErrorMessage(response);
    if (!apiError.empty()) {
        return "[Error] " + apiError;
    }
    // {
    //   "choices": [
    //     {
    //       "index": 0,
    //       "message": {
    //         "role": "assistant",
    //         "content": "这一轮要给用户看的文本（或工具 JSON）"
    //       },
    //       "finish_reason": "stop"
    //     }
    //   ]
    // }
    if (!response.contains("choices") || response["choices"].empty()) {
        return {};
    }
    const auto& message = response["choices"][0]["message"];
    if (message.contains("content") && message["content"].is_string()) {
        const auto content = message["content"].get<std::string>();
        if (!content.empty()) {
            return content;
        }
    }
    if (message.contains("reasoning_content") && message["reasoning_content"].is_string()) {
        return message["reasoning_content"].get<std::string>();
    }
    return {};
}

}  // namespace

std::string AliyunStrategy::getApiUrl() const {
    return "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
}

std::string AliyunStrategy::getApiKey()const {
    return apiKey_;
}


std::string AliyunStrategy::getModel() const {
    return kDashScopeChatModel;
}


json AliyunStrategy::buildRequest(const std::vector<std::pair<std::string, long long>>& messages) const {
    json payload;
    payload["model"] = getModel();
    payload["enable_thinking"] = false;
    json msgArray = json::array();

    for (size_t i = 0; i < messages.size(); ++i) {
        json msg;
        if (i % 2 == 0) {
            msg["role"] = "user";
        }
        else {
            msg["role"] = "assistant";
        }
        msg["content"] = messages[i].first;
        msgArray.push_back(msg);
    }
    payload["messages"] = msgArray;
    return payload;
}


std::string AliyunStrategy::parseResponse(const json& response) const {
    return parseDashScopeContent(response);
}


static StrategyRegister<AliyunStrategy> regAliyun("1");
