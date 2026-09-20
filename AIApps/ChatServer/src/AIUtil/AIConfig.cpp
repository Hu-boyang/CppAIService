#include"../include/AIUtil/AIConfig.h"

bool AIConfig::\






loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[AIConfig] Unable to open configuration file: " << path << std::endl;
        return false;
    }

    json j;
    file >> j;

    // Parsing templates
    if (!j.contains("prompt_template") || !j["prompt_template"].is_string()) {
        std::cerr << "[AIConfig] prompt_template is missing" << std::endl;
        return false;
    }
    promptTemplate_ = j["prompt_template"].get<std::string>();

    // List of parsing tools
    if (j.contains("tools") && j["tools"].is_array()) {
        for (auto& tool : j["tools"]) {
            AITool t;
            t.name = tool.value("name", "");
            t.desc = tool.value("desc", "");
            if (tool.contains("params") && tool["params"].is_object()) {
                for (auto& [key, val] : tool["params"].items()) {
                    t.params[key] = val.get<std::string>();
                }
            }
            tools_.push_back(std::move(t));
        }
    }
    return true;
}

std::string AIConfig::buildToolList() const {
    std::ostringstream oss;
    for (const auto& t : tools_) {
        oss << t.name << "(";
        bool first = true;
        for (const auto& [key, val] : t.params) {
            if (!first) oss << ", ";
            oss << key;
            first = false;
        }
        oss << ") -> " << t.desc << "\n";
    }
    return oss.str();
}

std::string AIConfig::buildPrompt(const std::string& userInput) const {
    std::string result = promptTemplate_;
    result = std::regex_replace(result, std::regex("\\{user_input\\}"), userInput);
    result = std::regex_replace(result, std::regex("\\{tool_list\\}"), buildToolList());
    return result;
}

namespace {

std::string extractFirstJsonObject(const std::string& text) {
    const auto start = text.find('{');
    if (start == std::string::npos) {
        return {};
    }
    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return text.substr(start, i - start + 1);
            }
        }
    }
    return {};
}

}  // namespace

AIToolCall AIConfig::parseAIResponse(const std::string& response) const {
    AIToolCall result;
    const std::string object = extractFirstJsonObject(response);
    if (object.empty()) {
        return result;
    }
    try {
        json j = json::parse(object);
        if (j.contains("tool") && j["tool"].is_string()) {
            result.toolName = j["tool"].get<std::string>();
            if (!result.toolName.empty()) {
                if (j.contains("args") && j["args"].is_object()) {
                    result.args = j["args"];
                }
                result.isToolCall = true;
            }
        }
    } catch (...) {
        result.isToolCall = false;
    }
    return result;
}

std::string AIConfig::buildFollowUpPrompt(
    const std::string& userInput,
    const json& toolHistory,
    bool forceAnswer) const
{
    std::ostringstream oss;
    oss << "下面是用户说的话：" << userInput << "\n"
        << "已调用的工具及返回结果：\n" << toolHistory.dump(4) << "\n";
    if (forceAnswer) {
        oss << "请不要再调用工具，根据以上结果用自然语言回答用户。";
    } else {
        oss << "若仍需另一个未使用过的工具，只输出 JSON："
            << "{\"tool\":\"工具名\",\"args\":{\"query\":\"检索词\"}}，不要输出其它文字。\n"
            << "若信息已足够，直接用自然语言回答用户，不要输出 JSON。";
    }
    return oss.str();
}

