#include "../include/handlers/ChatSpeechHandler.h"

#include <stdexcept>

void ChatSpeechHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
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

        std::string text;
        auto body = req.getBody();
        if (!body.empty()) {
            auto j = json::parse(body);
            jsonGetString(j, "text", text);
        }

        const char* secretEnv = std::getenv("BAIDU_CLIENT_SECRET");
        const char* idEnv = std::getenv("BAIDU_CLIENT_ID");
        if (!secretEnv) {
            throw std::runtime_error("BAIDU_CLIENT_SECRET not found!");
        }
        if (!idEnv) {
            throw std::runtime_error("BAIDU_CLIENT_ID not found!");
        }

        std::string clientId(idEnv);
        std::string clientSecret(secretEnv);
        AISpeechProcessor speechProcessor(clientId, clientSecret);
        std::string audio = speechProcessor.synthesize(text, "mp3-16k", "zh", 5, 5, 5);

        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(false);
        resp->setContentType("audio/mpeg");
        resp->addHeader("Cache-Control", "no-store");
        resp->setContentLength(audio.size());
        resp->setBody(audio);
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
