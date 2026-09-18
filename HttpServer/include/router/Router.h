#pragma once
#include <iostream>
#include <unordered_map>
#include <string>
#include <memory>
#include <functional>
#include <regex>
#include <vector>

#include "RouterHandler.h"
#include "../http/HttpRequest.h"
#include "../http/HttpResponse.h"

namespace http
{
namespace router
{

// Router：按「HTTP 方法 + 路径」把请求分发给对应处理逻辑。
//
// 两种处理器（同一条路由注册其一即可）：
//   HandlerPtr      对象式，继承 RouterHandler，适合登录/聊天等较复杂逻辑
//   HandlerCallback 回调函数，适合一两行就能写完的处理
//
// 两条匹配路径：
//   精确匹配  unordered_map，O(1)，如 POST /chat/send（本项目聊天接口都走这条）
//   动态匹配  vector + 正则，如 /users/:id，启动时把 :id 转成捕获组再逐条试
class Router
{
public:
    using HandlerPtr = std::shared_ptr<RouterHandler>;
    using HandlerCallback = std::function<void(const HttpRequest &, HttpResponse *)>;

    // 精确路由的键：方法（GET/POST/...）和完整 path 必须都相同才算同一条路由
    struct RouteKey
    {
        HttpRequest::Method method;
        std::string path;

        bool operator==(const RouteKey &other) const
        {
            return method == other.method && path == other.path;
        }
    };

    // 给 unordered_map 用的哈希：把 method、path 两个哈希合成一个 size_t
    struct RouteKeyHash
    {
        // size_t operator()(const RouteKey& key) const
        // {
        //     return std::hash<int>{}(static_cast<int>(key.method)) ^
        //            std::hash<std::string>{}(key.path);
        // }
        size_t operator()(const RouteKey &key) const
        {
            /*
                这里 std::hash<int>{}(static_cast<int>(key.method));
                相当于：
                std::hash<int> hasher;   
                size_t methodHash = hasher(static_cast<int>(key.method));   
                调用 operator()，返回 size_t
            */ 
            size_t methodHash = std::hash<int>{}(static_cast<int>(key.method));
            size_t pathHash = std::hash<std::string>{}(key.path);
            return methodHash * 31 + pathHash;
        }
    };

    // 注册路由处理器
    void registerHandler(HttpRequest::Method method, const std::string &path, HandlerPtr handler);

    // 注册回调函数形式的处理器
    void registerCallback(HttpRequest::Method method, const std::string &path, const HandlerCallback &callback);

    // 注册动态路由处理器
    void addRegexHandler(HttpRequest::Method method, const std::string &path, HandlerPtr handler)
    {
        std::regex pathRegex = convertToRegex(path);
        regexHandlers_.emplace_back(method, pathRegex, handler);
    }

    // 注册动态路由处理函数
    void addRegexCallback(HttpRequest::Method method, const std::string &path, const HandlerCallback &callback)
    {
        std::regex pathRegex = convertToRegex(path);
        regexCallbacks_.emplace_back(method, pathRegex, callback);
    }

    // 处理请求
    bool route(const HttpRequest &req, HttpResponse *resp);

private:
    std::regex convertToRegex(const std::string &pathPattern)
    { // 将路径模式转换为正则表达式，支持匹配任意路径参数
        std::string regexPattern = "^" + std::regex_replace(pathPattern, std::regex(R"(/:([^/]+))"), R"(/([^/]+))") + "$";
        return std::regex(regexPattern);
    }

    // 提取路径参数
    void extractPathParameters(const std::smatch &match, HttpRequest &request)
    {
        // Assuming the first match is the full path, parameters start from index 1
        for (size_t i = 1; i < match.size(); ++i)
        {
            request.setPathParameters("param" + std::to_string(i), match[i].str());
        }
    }

private:
    // 一条动态路由（回调版）：方法 + 已编译的路径正则 + 回调
    struct RouteCallbackObj
    {
        HttpRequest::Method method_;
        std::regex pathRegex_;
        HandlerCallback callback_;
        RouteCallbackObj(HttpRequest::Method method, std::regex pathRegex, const HandlerCallback &callback)
            : method_(method), pathRegex_(pathRegex), callback_(callback) {}
    };

    // 一条动态路由（对象版）：方法 + 已编译的路径正则 + Handler 对象
    struct RouteHandlerObj
    {
        HttpRequest::Method method_;
        std::regex pathRegex_;
        HandlerPtr handler_;
        RouteHandlerObj(HttpRequest::Method method, std::regex pathRegex, HandlerPtr handler)
            : method_(method), pathRegex_(pathRegex), handler_(handler) {}
    };

    // 精确：POST /chat/send → ChatSendHandler  用哈希表按 RouteKey 查找 哈希方式选择 RouteKeyHash
    std::unordered_map<RouteKey, HandlerPtr, RouteKeyHash>      handlers_;
    std::unordered_map<RouteKey, HandlerCallback, RouteKeyHash> callbacks_;
    // 动态：GET /users/:id → 正则 ^/users/([^/]+)$ ，匹配时抽出 pathParameters
    std::vector<RouteHandlerObj>                                regexHandlers_;
    std::vector<RouteCallbackObj>                               regexCallbacks_;
};


} // namespace router
} // namespace http