#pragma once
#include <string>
#include <memory>
#include "../http/HttpRequest.h"
#include "../http/HttpResponse.h"

namespace http
{
namespace router
{

// 对象式路由处理器的接口。ChatSendHandler 等都继承它，
// Router 匹配成功后调用 handle：从 req 读入，往 resp 填写响应。
class RouterHandler 
{
public:
    virtual ~RouterHandler() = default;
    virtual void handle(const HttpRequest& req, HttpResponse* resp) = 0;
};

} // namespace router
} // namespace http