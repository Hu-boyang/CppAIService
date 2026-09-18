#include "AIUtil/rag/BM25Index.h"
#include "AIUtil/rag/Utf8.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace rag {

void BM25Index::build(std::vector<TextChunk> chunks) {
    chunks_ = std::move(chunks);
    docLength_.assign(chunks_.size(), 0);
    inverted_.clear();
    avgdl_ = 0.0;

    if (chunks_.empty()) {
        return;
    }

    double lengthSum = 0.0;
    for (size_t i = 0; i < chunks_.size(); ++i) {
        auto tokens = tokenize(chunks_[i].text);
        docLength_[i] = static_cast<int>(tokens.size());
        lengthSum += static_cast<double>(tokens.size());

        std::unordered_map<std::string, int> tf;
        for (const auto& token : tokens) {
            ++tf[token];
        }
        for (const auto& [token, count] : tf) {
            inverted_[token].push_back({static_cast<int>(i), count});
        }
    }
    avgdl_ = lengthSum / static_cast<double>(chunks_.size());
}

std::vector<std::pair<int, double>> BM25Index::search(const std::string& query, int topK) const {
    std::vector<std::pair<int, double>> ranked;
    if (chunks_.empty() || topK <= 0) {
        return ranked;
    }

    auto queryTokens = tokenize(query);
    if (queryTokens.empty()) {
        return ranked;
    }

    std::unordered_set<std::string> uniqueTokens(queryTokens.begin(), queryTokens.end());
    const double N = static_cast<double>(chunks_.size());
    std::unordered_map<int, double> scores;

    for (const auto& token : uniqueTokens) {
        auto it = inverted_.find(token);
        if (it == inverted_.end()) {
            continue;
        }
        const auto& postings = it->second;
        const double df = static_cast<double>(postings.size());
        const double idf = std::log((N - df + 0.5) / (df + 0.5) + 1.0);
        for (const auto& posting : postings) {
            const double tf = static_cast<double>(posting.tf);
            const double dl = static_cast<double>(std::max(docLength_[posting.docId], 1));
            const double denom = tf + k1_ * (1.0 - b_ + b_ * (dl / std::max(avgdl_, 1.0)));
            scores[posting.docId] += idf * (tf * (k1_ + 1.0)) / denom;
        }
    }

    ranked.reserve(scores.size());
    for (const auto& [docId, score] : scores) {
        ranked.emplace_back(docId, score);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    if (static_cast<int>(ranked.size()) > topK) {
        ranked.resize(static_cast<size_t>(topK));
    }
    return ranked;
}

const TextChunk& BM25Index::chunkAt(size_t index) const {
    return chunks_.at(index);
}

}  // namespace rag
