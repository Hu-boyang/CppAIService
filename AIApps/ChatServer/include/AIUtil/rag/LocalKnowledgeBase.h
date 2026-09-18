#pragma once

#include "BM25Index.h"

#include <shared_mutex>
#include <string>
#include <vector>

namespace rag {

struct RagConfig {
    std::string kbDir = "kb";
    int chunkSize = 500;
    int chunkOverlap = 80;
    int topK = 4;
};

struct RetrievedChunk {
    std::string source;
    std::string text;
    double score = 0.0;
};

struct RagQueryResult {
    std::string systemPrompt;
    std::vector<RetrievedChunk> hits;
};

class LocalKnowledgeBase {
public:
    static LocalKnowledgeBase& instance();
    static RagQueryResult buildContext(const std::string& query);

    bool loadFromConfig(const std::string& configPath);
    bool load(const RagConfig& config);
    bool reload();

    std::vector<RetrievedChunk> search(const std::string& query) const;
    std::vector<RetrievedChunk> search(const std::string& query, int topK) const;

    bool isReady() const;
    size_t fileCount() const;
    size_t chunkCount() const;
    int topK() const;
    std::string kbDir() const;
    std::vector<std::string> listFiles() const;

private:
    LocalKnowledgeBase() = default;

    mutable std::shared_mutex mutex_;
    RagConfig config_;
    BM25Index index_;
    size_t fileCount_ = 0;
    bool ready_ = false;
};

}  // namespace rag
