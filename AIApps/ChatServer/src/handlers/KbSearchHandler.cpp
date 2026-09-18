#include "../include/handlers/KbSearchHandler.h"
#include "../include/AIUtil/rag/LocalKnowledgeBase.h"

void KbSearchHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    try
    {
        auto session = server_->getSessionManager()->getSession(req, resp);
        if (session->getValue("isLoggedIn") != "true")
        {
            json errorResp;
            errorResp["status"] = "error";
            errorResp["message"] = "Unauthorized";
            std::string errorBody = errorResp.dump(4);
            server_->packageResp(req.getVersion(), http::HttpResponse::k401Unauthorized,
                "Unauthorized", true, "application/json", errorBody.size(),
                errorBody, resp);
            return;
        }

        std::string query;
        auto body = req.getBody();
        if (!body.empty()) {
            auto j = json::parse(body);
            if (j.contains("query") && j["query"].is_string()) {
                query = j["query"].get<std::string>();
            }
        }
        if (query.empty()) {
            throw std::runtime_error("检索内容为空");
        }

        auto hits = rag::LocalKnowledgeBase::instance().search(query);
        json results = json::array();
        for (const auto& hit : hits) {
            json item;
            item["source"] = hit.source;
            item["text"] = hit.text;
            item["score"] = hit.score;
            results.push_back(item);
        }

        json successResp;
        successResp["success"] = true;
        successResp["results"] = results;
        std::string successBody = successResp.dump(4);

        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(false);
        resp->setContentType("application/json");
        resp->setContentLength(successBody.size());
        resp->setBody(successBody);
    }
    catch (const std::exception& e)
    {
        json failureResp;
        failureResp["status"] = "error";
        failureResp["message"] = e.what();
        std::string failureBody = failureResp.dump(4);
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(failureBody.size());
        resp->setBody(failureBody);
    }
}
