#include"../include/AIUtil/AIHelper.h"
#include"../include/AIUtil/MQManager.h"
#include"../include/AIUtil/ResourcePath.h"
#include <stdexcept>
#include<chrono>

namespace {

long long nowMs() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

}  // namespace

AIHelper::PendingScope::PendingScope(AIHelper& helper, const std::string& question)
    : helper_(helper)
{
    std::lock_guard<std::mutex> lock(helper_.stateMutex_);
    if (!helper_.pendingQuestion_.empty()) {
        throw SessionBusyError();
    }
    helper_.pendingQuestion_ = question;
}

AIHelper::PendingScope::~PendingScope() {
    std::lock_guard<std::mutex> lock(helper_.stateMutex_);
    helper_.pendingQuestion_.clear();
}

// 构造函数
AIHelper::AIHelper() {
    //默认使用阿里云大模型
    strategy = StrategyFactory::instance().create("1");
}

void AIHelper::setStrategy(std::shared_ptr<AIStrategy> strat) {
    strategy = strat;
}


// 设置默认模型
//void AIHelper::setModel(const std::string& modelName) {
  //  model_ = modelName;
//}

// 添加一条用户消息
void AIHelper::addMessage(int userId,const std::string& userName, bool is_user,const std::string& userInput, std::string sessionId) {
    const long long ms = nowMs();
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        messages.push_back({ userInput,ms });
    }
    //消息队列异步入库
    pushMessageToMysql(userId, userName, is_user, userInput, ms, sessionId);
}

void AIHelper::commitTurn(int userId, const std::string& userName, const std::string& sessionId,
    const std::string& question, const std::string& answer)
{
    const long long questionMs = nowMs();
    // 回答的时间戳必须严格大于提问，否则重启后按 ts 排序会把一问一答的顺序打乱
    const long long answerMs = questionMs + 1;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        messages.push_back({ question,questionMs });
        messages.push_back({ answer,answerMs });
        pendingQuestion_.clear();
    }
    pushMessageToMysql(userId, userName, true, question, questionMs, sessionId);
    pushMessageToMysql(userId, userName, false, answer, answerMs, sessionId);
}

void AIHelper::restoreMessage(const std::string& userInput,long long ms) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    messages.push_back({ userInput,ms });
}

void AIHelper::mergeKnowledgeSources(const json& toolResult) {
    if (!toolResult.contains("hits") || !toolResult["hits"].is_array()) {
        return;
    }
    for (const auto& hit : toolResult["hits"]) {
        const std::string source = hit.value("source", std::string());
        if (source.empty()) {
            continue;
        }
        const double score = hit.value("score", 0.0);
        bool exists = false;
        for (auto& item : lastRagSources_) {
            if (item.value("source", std::string()) == source) {
                exists = true;
                if (score > item.value("score", 0.0)) {
                    item["score"] = score;
                }
                break;
            }
        }
        if (!exists) {
            json src;
            src["source"] = source;
            src["score"] = score;
            lastRagSources_.push_back(std::move(src));
        }
    }
}

// 发送聊天消息
std::string AIHelper::chat(int userId,std::string userName, std::string sessionId, std::string userQuestion) {
    setStrategy(StrategyFactory::instance().create("1"));

    // 整个问答期间把会话标记为忙碌，前端据此恢复“正在思考”
    PendingScope pending(*this, userQuestion);
    lastRagSources_ = json::array();

    AIConfig config;
    config.loadFromFile(chatResourceFile("config.json"));

    AIToolRegistry registry;
    json toolHistory = json::array();
    std::string prompt = config.buildPrompt(userQuestion);
    std::string lastText;
    constexpr int kMaxToolsPerRound = 3;
    constexpr int kMaxLlmRounds = 8;
    int llmRounds = 0;

    const auto history = GetMessages();

    while (true) {
        // 提示词只属于本次请求，拼在副本上，不能污染会话历史
        auto request = history;
        request.push_back({ prompt, 0 });
        json req = strategy->buildRequest(request);
        json resp = executeCurl(req);
        lastText = strategy->parseResponse(resp);

        if (lastText.empty()) {
            lastText = "[Error] 无法解析响应";
            break;
        }

        ++llmRounds;

        const bool lastAllowedRound = llmRounds >= kMaxLlmRounds;
        std::vector<AIToolCall> calls;
        if (!lastAllowedRound) {
            calls = config.parseToolCalls(lastText);
        }

        std::vector<AIToolCall> pending;
        for (auto& call : calls) {
            if (!registry.hasTool(call.toolName)) {
                continue;
            }
            pending.push_back(std::move(call));
        }
        if (pending.empty()) {
            break;
        }

        if (static_cast<int>(pending.size()) > kMaxToolsPerRound) {
            pending.resize(static_cast<size_t>(kMaxToolsPerRound));
        }

        for (const auto& call : pending) {
            json toolResult;
            try {
                toolResult = registry.invoke(call.toolName, call.args);
                std::cout << "Tool call success: " << call.toolName << std::endl;
            } catch (const std::exception& e) {
                toolResult = json{ {"error", e.what()} };
                std::cout << "Tool call failed: " << e.what() << std::endl;
            }

            if (call.toolName == "search_knowledge") {
                mergeKnowledgeSources(toolResult);
            }

            json entry;
            entry["tool"] = call.toolName;
            entry["args"] = call.args;
            entry["result"] = toolResult;
            toolHistory.push_back(std::move(entry));
        }
        const bool forceAnswer = llmRounds + 1 >= kMaxLlmRounds;
        prompt = config.buildFollowUpPrompt(userQuestion, toolHistory, forceAnswer);
    }

    commitTurn(userId, userName, sessionId, userQuestion, lastText);
    return lastText;
}

// 发送自定义请求体
json AIHelper::request(const json& payload) {
    return executeCurl(payload);
}

std::vector<std::pair<std::string, long long>> AIHelper::GetMessages() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return this->messages;
}

std::string AIHelper::pendingQuestion() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return pendingQuestion_;
}


// 内部方法：执行 curl 请求
json AIHelper::executeCurl(const json& payload) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }

    const std::string apiKey = strategy->getApiKey();
    std::cout << "dashscope " << strategy->getApiUrl()
              << " key_len=" << apiKey.size() << std::endl;

    std::string readBuffer;
    struct curl_slist* headers = nullptr;
    std::string authHeader = "Authorization: Bearer " + apiKey;

    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    // POST 超过 1KB 时 libcurl 会发 Expect: 100-continue，部分网关一直不回 100，拖到总超时。
    headers = curl_slist_append(headers, "Expect:");

    std::string payloadStr = payload.dump();

    // 对 curl 选项进行设置
    curl_easy_setopt(curl, CURLOPT_URL, strategy->getApiUrl().c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payloadStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payloadStr.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);

    // 这段代码执行 curl 指令
    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    double totalTime = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalTime);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error("curl_easy_perform() failed: "
            + std::string(curl_easy_strerror(res))
            + " (" + std::to_string(static_cast<int>(totalTime)) + "s)");
    }

    json parsed;
    try {
        parsed = json::parse(readBuffer);
    } catch (...) {
        throw std::runtime_error("Failed to parse JSON response HTTP "
            + std::to_string(httpCode) + ": " + readBuffer);
    }

    if (httpCode >= 400 && parsed.contains("error")) {
        return parsed;
    }
    if (httpCode >= 400) {
        std::string message = "HTTP " + std::to_string(httpCode);
        if (parsed.contains("message") && parsed["message"].is_string()) {
            message += ": " + parsed["message"].get<std::string>();
        }
        parsed["error"] = json{{"message", message}, {"code", std::to_string(httpCode)}};
    }
    return parsed;
}

// curl 回调函数，把返回的数据写到 string buffer
size_t AIHelper::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

std::string AIHelper::escapeString(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 2);
    for (char c : input) {
        switch (c) {
            case '\\': output += "\\\\"; break;
            case '\'': output += "\\\'"; break;
            case '\"': output += "\\\""; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:   output += c; break;
        }
    }
    return output;
}


void AIHelper::pushMessageToMysql(int userId, const std::string& userName, bool is_user, const std::string& userInput,long long ms, std::string sessionId) {
    // std::string sql = "INSERT INTO chat_message (id, username, is_user, content, ts) VALUES ("
    //     + std::to_string(userId) + ", "  // 这里用 userId 作为 id，或者你自己生成
    //     + "'" + userName + "', "
    //     + std::to_string(is_user ? 1 : 0) + ", "
    //     + "'" + userInput + "', "
    //     + std::to_string(ms) + ")";
    std::string safeUserName = escapeString(userName);
    std::string safeUserInput = escapeString(userInput);

    std::string sql = "INSERT INTO chat_message (id, username, session_id, is_user, content, ts) VALUES ("
        + std::to_string(userId) + ", "
        + "'" + safeUserName + "', "
        + sessionId + ", "
        + std::to_string(is_user ? 1 : 0) + ", "
        + "'" + safeUserInput + "', "
        + std::to_string(ms) + ")";

    //改成消息队列异步执行mysql操作，用于流量削峰与解耦逻辑
    //mysqlUtil_.executeUpdate(sql);

    MQManager::instance().publish("sql_queue", sql);
}

