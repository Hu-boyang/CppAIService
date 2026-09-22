#include "AIUtil/rag/LocalKnowledgeBase.h"
#include "AIUtil/rag/DocumentLoader.h"
#include "AIUtil/rag/TextChunker.h"
#include "utils/JsonUtil.h"

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

void readIntField(const json& obj, const char* key, int& out) {
    if (obj.contains(key) && obj[key].is_number_integer()) {
        out = obj[key].get<int>();
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
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "[RAG] config not found, using defaults: " << configPath << std::endl;
        config.kbDir = resolveKbDir(config.kbDir, configPath);
        return load(config);
    }

    try {
        json j;
        file >> j;
        if (j.contains("rag") && j["rag"].is_object()) {
            const json& ragObj = j["rag"];
            jsonGetString(ragObj, "kb_dir", config.kbDir);
            readIntField(ragObj, "chunk_size", config.chunkSize);
            readIntField(ragObj, "chunk_overlap", config.chunkOverlap);
            readIntField(ragObj, "top_k", config.topK);
        }
    } catch (const std::exception& e) {
        std::cerr << "[RAG] config parse failed, using defaults: " << e.what() << std::endl;
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
    for (const auto& [chunkId, score] : ranked) {
        const auto& chunk = index_.chunkAt(chunkId);
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
