#include "AIUtil/rag/LocalKnowledgeBase.h"
#include "AIUtil/rag/DocumentLoader.h"
#include "AIUtil/rag/TextChunker.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <utility>

namespace rag {
namespace {

std::string readAll(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string extractObject(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = text.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    pos = text.find('{', pos + needle.size());
    if (pos == std::string::npos) {
        return {};
    }
    int depth = 0;
    for (size_t i = pos; i < text.size(); ++i) {
        if (text[i] == '{') {
            ++depth;
        } else if (text[i] == '}') {
            --depth;
            if (depth == 0) {
                return text.substr(pos, i - pos + 1);
            }
        }
    }
    return {};
}

bool extractString(const std::string& obj, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    auto pos = obj.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = obj.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    pos = obj.find('"', pos + 1);
    if (pos == std::string::npos) {
        return false;
    }
    auto end = obj.find('"', pos + 1);
    if (end == std::string::npos) {
        return false;
    }
    out = obj.substr(pos + 1, end - pos - 1);
    return true;
}

bool extractInt(const std::string& obj, const std::string& key, int& out) {
    const std::string needle = "\"" + key + "\"";
    auto pos = obj.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = obj.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    pos = obj.find_first_of("0123456789-", pos + 1);
    if (pos == std::string::npos) {
        return false;
    }
    try {
        out = std::stoi(obj.substr(pos));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

LocalKnowledgeBase& LocalKnowledgeBase::instance() {
    static LocalKnowledgeBase kb;
    return kb;
}

RagQueryResult LocalKnowledgeBase::buildContext(const std::string& query) {
    RagQueryResult result;
    result.hits = instance().search(query);
    std::ostringstream context;
    context << "你是一个基于本地知识库回答问题的助手。请仅根据下列资料回答用户问题。"
            << "如果资料不足以回答，请明确说明不知道，不要编造。"
            << "回答时可引用资料编号，格式如 [1] 文件名。\n\n【资料】\n";
    if (result.hits.empty()) {
        context << "（知识库中未检索到相关内容）\n";
    } else {
        for (size_t i = 0; i < result.hits.size(); ++i) {
            context << "[" << (i + 1) << "] " << result.hits[i].source << "\n"
                    << result.hits[i].text << "\n\n";
        }
    }
    result.systemPrompt = context.str();
    return result;
}

std::string resolveKbDir(const std::string& kbDir, const std::string& configPath) {
    std::error_code ec;
    std::filesystem::path configured(kbDir);
    auto existsDir = [&](const std::filesystem::path& p) {
        return std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec);
    };
    if (configured.is_absolute() && existsDir(configured)) {
        return configured.lexically_normal().string();
    }

    std::vector<std::filesystem::path> candidates;
    candidates.push_back(configured);
    if (!configPath.empty()) {
        auto configDir = std::filesystem::path(configPath).parent_path();
        candidates.push_back(configDir / configured);
        candidates.push_back(configDir / "kb");
    }
    for (const auto& candidate : candidates) {
        if (existsDir(candidate)) {
            return std::filesystem::weakly_canonical(candidate, ec).string();
        }
    }
    return kbDir;
}

bool LocalKnowledgeBase::loadFromConfig(const std::string& configPath) {
    RagConfig config;
    const std::string body = readAll(configPath);
    if (body.empty()) {
        std::cerr << "[RAG] config not found, using defaults: " << configPath << std::endl;
        config.kbDir = resolveKbDir(config.kbDir, configPath);
        return load(config);
    }

    const std::string ragObj = extractObject(body, "rag");
    if (!ragObj.empty()) {
        extractString(ragObj, "kb_dir", config.kbDir);
        extractInt(ragObj, "chunk_size", config.chunkSize);
        extractInt(ragObj, "chunk_overlap", config.chunkOverlap);
        extractInt(ragObj, "top_k", config.topK);
    }
    config.kbDir = resolveKbDir(config.kbDir, configPath);
    return load(config);
}

bool LocalKnowledgeBase::load(const RagConfig& config) {
    auto docs = DocumentLoader::loadDirectory(config.kbDir);
    TextChunker chunker(config.chunkSize, config.chunkOverlap);
    std::vector<TextChunk> chunks;
    for (const auto& doc : docs) {
        auto parts = chunker.chunk(doc);
        chunks.insert(chunks.end(), parts.begin(), parts.end());
    }

    BM25Index index;
    index.build(chunks);
    const size_t chunkCount = chunks.size();
    const size_t fileCount = docs.size();

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        config_ = config;
        index_ = std::move(index);
        fileCount_ = fileCount;
        ready_ = true;
    }

    std::cout << "[RAG] loaded files=" << fileCount
              << " chunks=" << chunkCount
              << " dir=" << config.kbDir << std::endl;
    return true;
}

std::vector<RetrievedChunk> LocalKnowledgeBase::search(const std::string& query) const {
    return search(query, topK());
}

std::vector<RetrievedChunk> LocalKnowledgeBase::search(const std::string& query, int topK) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<RetrievedChunk> results;
    if (!ready_ || index_.size() == 0) {
        return results;
    }

    auto ranked = index_.search(query, topK);
    results.reserve(ranked.size());
    for (const auto& [docId, score] : ranked) {
        const auto& chunk = index_.chunkAt(static_cast<size_t>(docId));
        results.push_back({chunk.source, chunk.text, score});
    }
    return results;
}

bool LocalKnowledgeBase::isReady() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return ready_;
}

size_t LocalKnowledgeBase::fileCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return fileCount_;
}

size_t LocalKnowledgeBase::chunkCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return index_.size();
}

int LocalKnowledgeBase::topK() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return config_.topK > 0 ? config_.topK : 4;
}

bool LocalKnowledgeBase::reload() {
    RagConfig config;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        config = config_;
    }
    if (config.kbDir.empty()) {
        return false;
    }
    return load(config);
}

std::string LocalKnowledgeBase::kbDir() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return config_.kbDir;
}

std::vector<std::string> LocalKnowledgeBase::listFiles() const {
    std::string dir;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        dir = config_.kbDir;
    }

    std::vector<std::string> files;
    std::error_code ec;
    std::filesystem::path root(dir);
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return files;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec || !entry.is_regular_file(ec)) {
            continue;
        }
        auto ext = entry.path().extension().string();
        for (char& c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext != ".md" && ext != ".txt") {
            continue;
        }
        std::string relative = std::filesystem::relative(entry.path(), root, ec).generic_string();
        if (ec || relative.empty()) {
            relative = entry.path().filename().string();
        }
        files.push_back(std::move(relative));
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace rag
