#pragma once
#include <string>
#include <vector>
#include <utility>
#include <mutex>
#include <stdexcept>
#include <curl/curl.h>
#include <iostream>
#include <sstream>

#include "../../../../HttpServer/include/utils/JsonUtil.h"
#include"../../../../HttpServer/include/utils/MysqlUtil.h"

#include"AIFactory.h"
#include"AIConfig.h"
#include"AIToolRegistry.h"

class SessionBusyError : public std::runtime_error {
public:
    SessionBusyError()
        : std::runtime_error("该会话正在回答，请稍候或新开对话") {}
};


//这边封装curl去访问对阿里的模型
class AIHelper {
public:
    // 构造函数，初始化API Key
    AIHelper();

    // 设置默认模型
    //void setModel(const std::string& modelName);

    void setStrategy(std::shared_ptr<AIStrategy> strat);

    // 恢复一条消息
    void restoreMessage(const std::string& userInput, long long ms);

    // 发送聊天消息，返回AI的响应内容
    std::string chat(int userId, std::string userName, std::string sessionId, std::string userQuestion);
    json lastRagSources() const { return lastRagSources_; }

    // 可选：发送自定义请求体
    json request(const json& payload);

    std::vector<std::pair<std::string, long long>> GetMessages();

    // 模型正在回答时返回对应的问题，空串表示该会话空闲
    std::string pendingQuestion() const;

private:
    // 一问一答同时入列并清除忙碌标记，读取方不会看到半个回合
    void commitTurn(int userId, const std::string& userName, const std::string& sessionId,
        const std::string& question, const std::string& answer);

    // chat() 期间标记会话忙碌，抛异常时由析构兜底清除
    class PendingScope {
    public:
        PendingScope(AIHelper& helper, const std::string& question);
        ~PendingScope();
        PendingScope(const PendingScope&) = delete;
        PendingScope& operator=(const PendingScope&) = delete;

    private:
        AIHelper& helper_;
    };

    std::string escapeString(const std::string& input);
    //加入到mysql的接口（提供加入到线程池的接口，线程池做异步mysql更新操作）
    //todo: 
    void pushMessageToMysql(int userId, const std::string& userName, bool is_user, const std::string& userInput, long long ms,std::string sessionId);

    void mergeKnowledgeSources(const json& toolResult);

    // 内部方法：执行curl请求，返回原始JSON
    json executeCurl(const json& payload);
    // curl 回调函数，把返回的数据写到 string buffer
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

private:

    /*
    * 重构代码，将其使用策略模式&&工厂模式抽离出来
    std::string apiKey_;
    //默认用通义千问
    std::string model_ = "qwen3.8-27b";
    //对应地址
    std::string apiUrl_ = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
    */
    std::shared_ptr<AIStrategy> strategy;
    json lastRagSources_ = json::array();

    //一个用户针对一个AIHelper，messages存放用户的历史对话
    //偶数下标代表用户的信息，奇数下标是ai返回的内容
    //后者代表时间戳
    //只存放已完成的对话，工具提示词等临时内容不允许写进来
    std::vector<std::pair<std::string, long long>> messages;

    //messages 与 pendingQuestion_ 会被多个 IO 线程同时读写
    mutable std::mutex stateMutex_;
    std::string pendingQuestion_;

    //http::MysqlUtil mysqlUtil_;
};
