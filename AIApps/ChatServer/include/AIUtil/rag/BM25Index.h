#pragma once

#include "TextChunker.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rag {

class BM25Index {
public:
    void build(std::vector<TextChunk> chunks);
    std::vector<std::pair<size_t, double>> search(const std::string& query, int topK) const;

    const TextChunk& chunkAt(size_t index) const;
    size_t size() const { return chunks_.size(); }

private:
    struct Posting {
        size_t chunkId = 0;
        size_t tf = 0;
    };

    std::vector<TextChunk> chunks_;
    std::vector<size_t> chunkLength_;
    std::unordered_map<std::string, std::vector<Posting>> inverted_;
    double avgdl_ = 0.0;
    double k1_ = 1.5;
    double b_ = 0.75;
};

}  // namespace rag
