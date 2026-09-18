#include "../include/handlers/KbUploadHandler.h"
#include "../include/AIUtil/rag/LocalKnowledgeBase.h"

#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace {

constexpr size_t kMaxKbBytes = 1024 * 1024;

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string safeKbFilename(const std::string& raw) {
    std::string name = std::filesystem::path(raw).filename().string();
    if (name.empty() || name == "." || name == "..") {
        throw std::runtime_error("文件名无效");
    }
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        throw std::runtime_error("文件名不能包含路径");
    }
    const std::string ext = toLowerCopy(std::filesystem::path(name).extension().string());
    if (ext != ".txt" && ext != ".md") {
        throw std::runtime_error("只支持 .txt 或 .md 文档");
    }
    return name;
}

}  // namespace

void KbUploadHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
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

        std::string filename;
        std::string content;
        auto body = req.getBody();
        if (!body.empty()) {
            auto j = json::parse(body);
            if (j.contains("filename") && j["filename"].is_string()) {
                filename = j["filename"].get<std::string>();
            }
            if (j.contains("content") && j["content"].is_string()) {
                content = j["content"].get<std::string>();
            }
        }
        filename = safeKbFilename(filename);
        if (content.empty()) {
            throw std::runtime_error("文档内容为空");
        }
        if (content.size() > kMaxKbBytes) {
            throw std::runtime_error("文档超过 1MB 限制");
        }

        auto& kb = rag::LocalKnowledgeBase::instance();
        std::string dir = kb.kbDir();
        if (dir.empty()) {
            dir = chatResourceFile("kb");
        }
        std::filesystem::create_directories(dir);
        const auto path = std::filesystem::path(dir) / filename;
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) {
                throw std::runtime_error("无法写入文档: " + filename);
            }
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
        if (!kb.reload()) {
            throw std::runtime_error("文档已保存，但索引重建失败");
        }

        json successResp;
        successResp["success"] = true;
        successResp["filename"] = filename;
        successResp["fileCount"] = kb.fileCount();
        successResp["chunkCount"] = kb.chunkCount();
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
