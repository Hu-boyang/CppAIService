#include"../include/AIUtil/AIConfig.h"

#include <unordered_set>

bool AIConfig::loadFromFile(const std::string& path) {
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

std::string extractBalancedJson(const std::string& text, size_t& pos) {
    while (pos < text.size() && text[pos] != '{' && text[pos] != '[') {
        ++pos;
    }
    if (pos >= text.size()) {
        return {};
    }

    const char open = text[pos];
    const char close = (open == '{') ? '}' : ']';
    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (size_t i = pos; i < text.size(); ++i) {
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
        } else if (c == open) {
            ++depth;
        } else if (c == close) {
            --depth;
            if (depth == 0) {
                const std::string value = text.substr(pos, i - pos + 1);
                pos = i + 1;
                return value;
            }
        }
    }
    return {};
}

bool parseOneToolCall(const json& node, AIToolCall& call) {
    if (!node.is_object() || !node.contains("tool") || !node["tool"].is_string()) {
        return false;
    }
    call.toolName = node["tool"].get<std::string>();
    if (call.toolName.empty()) {
        return false;
    }
    if (node.contains("args") && node["args"].is_object()) {
        call.args = node["args"];
    } else {
        // 就是让 args = {}
        call.args = json::object();
    }
    return true;
}

void appendToolCalls(const json& node, std::vector<AIToolCall>& calls) {
    if (node.is_array()) {
        for (const auto& item : node) {
            appendToolCalls(item, calls);
        }
        return;
    }
    if (!node.is_object()) {
        return;
    }
    if (node.contains("tools") && node["tools"].is_array()) {
        for (const auto& item : node["tools"]) {
            AIToolCall call;
            if (parseOneToolCall(item, call)) {
                calls.push_back(std::move(call));
            }
        }
        return;
    }
    AIToolCall call;
    if (parseOneToolCall(node, call)) {
        calls.push_back(std::move(call));
    }
}

}  // namespace

std::vector<AIToolCall> AIConfig::parseToolCalls(const std::string& response) const {
    std::vector<AIToolCall> calls;
    size_t pos = 0;
    while (pos < response.size()) {
        const std::string chunk = extractBalancedJson(response, pos);
        if (chunk.empty()) {
            break;
        }
        try {
            appendToolCalls(json::parse(chunk), calls);
        } catch (...) {
        }
    }

    std::vector<AIToolCall> unique;
    std::unordered_set<std::string> seen;
    unique.reserve(calls.size());
    seen.reserve(calls.size());
    for (auto& call : calls) {
        const std::string key = call.toolName + '\n' + call.args.dump();
        if (seen.insert(key).second) {
            unique.push_back(std::move(call));
        }
    }
    return unique;
}

std::string AIConfig::buildFollowUpPrompt(
    const std::string& userInput,
    const json& toolHistory,
    bool forceAnswer) const
{
    std::ostringstream oss;
    oss << "下面是用户说的话：" << userInput << "\n"
        << "已调用的工具及返回结果：\n" << toolHistory.dump() << "\n";
    if (forceAnswer) {
        oss << "请不要再调用工具，根据以上结果用自然语言回答用户。";
    } else {
        oss << "若仍需工具且不依赖未给出的中间结果，可一次输出多个（每次回复最多 3 个）："
            << "{\"tools\":[{\"tool\":\"工具名\",\"args\":{\"query\":\"检索词\"}}]}，不要输出其它文字。\n"
            << "不要重复相同的工具和检索词。还需要更多工具时下一轮再调用。"
            << "若信息已足够，直接用自然语言回答用户，不要输出 JSON。";
    }
    return oss.str();
}

