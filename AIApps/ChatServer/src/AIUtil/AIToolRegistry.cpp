#include "../include/AIUtil/AIToolRegistry.h"
#include "../include/AIUtil/EnvUtil.h"
#include "../include/AIUtil/rag/LocalKnowledgeBase.h"
#include <sstream>


AIToolRegistry::AIToolRegistry() {
    registerTool("search_knowledge", searchKnowledge);
    registerTool("search_web", searchWeb);
}


void AIToolRegistry::registerTool(const std::string& name, ToolFunc func) {
    tools_[name] = std::move(func);
}


json AIToolRegistry::invoke(const std::string& name, const json& args) const {
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        throw std::runtime_error("Tool not found: " + name);
    }
    return it->second(args);
}


bool AIToolRegistry::hasTool(const std::string& name) const {
    return tools_.count(name) > 0;
}


size_t AIToolRegistry::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}


std::string AIToolRegistry::extractQuery(const json& args) {
    if (args.contains("query") && args["query"].is_string()) {
        return args["query"].get<std::string>();
    }
    if (args.contains("q") && args["q"].is_string()) {
        return args["q"].get<std::string>();
    }
    return {};
}


json AIToolRegistry::searchKnowledge(const json& args) {
    const std::string query = extractQuery(args);
    if (query.empty()) {
        return json{ {"error", "Missing parameter: query"} };
    }

    auto& kb = rag::LocalKnowledgeBase::instance();
    json hits = json::array();
    if (kb.isReady()) {
        for (const auto& hit : kb.search(query)) {
            json item;
            item["source"] = hit.source;
            item["text"] = hit.text;
            item["score"] = hit.score;
            hits.push_back(std::move(item));
        }
    }

    json result;
    result["query"] = query;
    result["ready"] = kb.isReady();
    const auto hitCount = hits.size();
    result["hit_count"] = hitCount;
    result["hits"] = std::move(hits);
    if (hitCount == 0) {
        result["note"] = kb.isReady()
            ? "知识库中未检索到相关内容，不要编造文档内容。"
            : "知识库尚未就绪。";
    }
    return result;
}


json AIToolRegistry::searchWeb(const json& args) {
    const std::string query = extractQuery(args);
    if (query.empty()) {
        return json{ {"error", "Missing parameter: query"} };
    }

    const char* key = std::getenv("DASHSCOPE_API_KEY");
    if (!key || !*key) {
        return json{ {"error", "未配置 DASHSCOPE_API_KEY，无法联网搜索"} };
    }

    json payload;
    payload["model"] = envOr("DASHSCOPE_CHAT_MODEL", "qwen3.8-27b");
    payload["enable_thinking"] = false;
    payload["enable_search"] = true;
    json msg;
    msg["role"] = "user";
    msg["content"] = "请根据互联网搜索结果，针对以下问题给出简洁事实摘要，并列出关键来源标题与 URL。"
                     "不要编造来源。问题：" + query;
    payload["messages"] = json::array({ msg });
    const std::string body = payload.dump();

    CURL* curl = curl_easy_init();
    if (!curl) {
        return json{ {"error", "Failed to init CURL"} };
    }

    std::string response;
    struct curl_slist* headers = nullptr;
    const std::string auth = "Authorization: Bearer " + std::string(key);
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Expect:");

    curl_easy_setopt(curl, CURLOPT_URL,
                     "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);

    const CURLcode rc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        return json{ {"error", std::string("CURL request failed: ") + curl_easy_strerror(rc)} };
    }

    json root;
    try {
        root = json::parse(response);
    } catch (...) {
        return json{
            {"error", "联网搜索返回无法解析"},
            {"http_code", httpCode}
        };
    }

    if (root.contains("error")) {
        const auto& err = root["error"];
        if (err.is_string()) {
            return json{ {"error", err.get<std::string>()} };
        }
        if (err.is_object() && err.contains("message") && err["message"].is_string()) {
            return json{ {"error", err["message"].get<std::string>()} };
        }
        return json{ {"error", err.dump()} };
    }

    std::string summary;
    if (root.contains("choices") && !root["choices"].empty()) {
        const auto& message = root["choices"][0]["message"];
        if (message.contains("content") && message["content"].is_string()) {
            summary = message["content"].get<std::string>();
        }
    }
    if (summary.empty()) {
        return json{
            {"error", "联网搜索未返回内容"},
            {"http_code", httpCode}
        };
    }

    json result;
    result["query"] = query;
    result["summary"] = summary;
    return result;
}
