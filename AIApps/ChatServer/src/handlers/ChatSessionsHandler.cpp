#include "../include/handlers/ChatSessionsHandler.h"
#include "../include/AIUtil/rag/Utf8.h"

namespace {

std::string collapseSpaces(std::string text) {
    std::string out;
    out.reserve(text.size());
    bool space = false;
    for (char ch : text) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
        if (ch == ' ') {
            if (!space) {
                out.push_back(' ');
            }
            space = true;
        } else {
            out.push_back(ch);
            space = false;
        }
    }
    return rag::trimCopy(out);
}

std::string summarize(const std::string& text) {
    const std::string title = collapseSpaces(text);
    if (title.empty()) {
        return {};
    }
    constexpr size_t kMaxChars = 24;
    if (rag::utf8Length(title) <= kMaxChars) {
        return title;
    }
    return rag::utf8Substring(title, 0, kMaxChars) + "…";
}

std::string sessionSummaryFromMessages(const std::vector<std::pair<std::string, long long>>& messages) {
    for (const auto& msg : messages) {
        const std::string title = summarize(msg.first);
        if (!title.empty()) {
            return title;
        }
    }
    return {};
}

}  // namespace

void ChatSessionsHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    try
    {
        auto session = server_->getSessionManager()->getSession(req, resp);
        LOG_INFO << "session->getValue(\"isLoggedIn\") = " << session->getValue("isLoggedIn");
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

        int userId = std::stoi(session->getValue("userId"));
        std::vector<std::string> sessions;
        json sessionArray = json::array();
        {
            std::lock_guard<std::mutex> lock(server_->mutexForChatInformation);
            std::lock_guard<std::mutex> lockIds(server_->mutexForSessionsId);
            sessions = server_->sessionsIdsMap[userId];
            auto& userSessions = server_->chatInformation[userId];
            for (const auto& sid : sessions) {
                json s;
                s["sessionId"] = sid;
                std::string name;
                std::string pendingQuestion;
                auto it = userSessions.find(sid);
                if (it != userSessions.end() && it->second) {
                    pendingQuestion = it->second->pendingQuestion();
                    const auto messages = it->second->GetMessages();
                    name = sessionSummaryFromMessages(messages);
                    if (name.empty()) {
                        name = summarize(pendingQuestion);
                    }
                    s["hasMessages"] = !messages.empty();
                } else {
                    s["hasMessages"] = false;
                }
                s["name"] = name.empty() ? "新会话" : name;
                s["pending"] = !pendingQuestion.empty();
                sessionArray.push_back(s);
            }
        }

        json successResp;
        successResp["success"] = true;
        successResp["sessions"] = sessionArray;
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
