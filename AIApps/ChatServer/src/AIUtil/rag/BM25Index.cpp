#include "AIUtil/rag/BM25Index.h"
#include "AIUtil/rag/Utf8.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace rag {

void BM25Index::build(std::vector<TextChunk> chunks) {
    chunks_ = std::move(chunks);
    chunkLength_.assign(chunks_.size(), 0);
    inverted_.clear();
    avgdl_ = 0.0;

    if (chunks_.empty()) {
        return;
    }

    size_t lengthSum = 0;
    for (size_t i = 0; i < chunks_.size(); ++i) {
        auto tokens = tokenize(chunks_[i].text);
        chunkLength_[i] = tokens.size();
        lengthSum += tokens.size();

        std::unordered_map<std::string, size_t> tf;
        for (const auto& token : tokens) {
            ++tf[token];
        }
        // 某个 token 在那些块中出现的多
        for (const auto& [token, count] : tf) {
            inverted_[token].push_back({i, count});
        }
    }
    avgdl_ = static_cast<double>(lengthSum) / static_cast<double>(chunks_.size());
}

// topK 表示最多把几段文本交给模型去处理
std::vector<std::pair<size_t, double>> BM25Index::search(const std::string& query, int topK) const {
    std::vector<std::pair<size_t, double>> ranked;
    if (chunks_.empty() || topK <= 0) {
        return ranked;
    }

    auto queryTokens = tokenize(query);
    if (queryTokens.empty()) {
        return ranked;
    }

    std::unordered_set<std::string> uniqueTokens(queryTokens.begin(), queryTokens.end());
    const double N = static_cast<double>(chunks_.size());

    // 每个 chunks 在所有 token 中的总得分
    std::unordered_map<size_t, double> scores;

    for (const auto& token : uniqueTokens) {

        // inverted 代表一个 token 在所有块儿出现的分别次数的总和 token，vector<chunk_id, count>
        auto it = inverted_.find(token);
        if (it == inverted_.end()) {
            continue;
        }
        const auto& postings = it->second;
        const double df = static_cast<double>(postings.size());
        const double idf = std::log((N - df + 0.5) / (df + 0.5) + 1.0);
        for (const auto& posting : postings) {
            // 这里就是根据 BM25 公式计算每个块的得分 其实感觉更像是 token 出现次数和块儿长度的比值
            const double tf = static_cast<double>(posting.tf);
            const double dl = static_cast<double>(std::max(chunkLength_[posting.chunkId], size_t{1}));
            const double denom = tf + k1_ * (1.0 - b_ + b_ * (dl / std::max(avgdl_, 1.0)));
            scores[posting.chunkId] += idf * (tf * (k1_ + 1.0)) / denom;
        }
    }

    ranked.reserve(scores.size());
    for (const auto& [chunkId, score] : scores) {
        ranked.emplace_back(chunkId, score);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    if (ranked.size() > static_cast<size_t>(topK)) {
        ranked.resize(static_cast<size_t>(topK));
    }
    return ranked;
}

const TextChunk& BM25Index::chunkAt(size_t index) const {
    return chunks_.at(index);
}

}  // namespace rag
