#include "AIUtil/rag/TextChunker.h"
#include "AIUtil/rag/Utf8.h"

namespace rag {
namespace {

std::vector<std::string> splitParagraphs(const std::string& text) {
    std::vector<std::string> paragraphs;
    std::string current;
    current.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            continue;
        }
        if (text[i] == '\n') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                auto trimmed = trimCopy(current);
                if (!trimmed.empty()) {
                    paragraphs.push_back(std::move(trimmed));
                }
                current.clear();
                ++i;
                continue;
            }
            current.push_back('\n');
            continue;
        }
        current.push_back(text[i]);
    }
    auto trimmed = trimCopy(current);
    if (!trimmed.empty()) {
        paragraphs.push_back(std::move(trimmed));
    }
    return paragraphs;
}

}  // namespace

TextChunker::TextChunker(int chunkSize, int overlap)
    : chunkSize_(chunkSize > 0 ? chunkSize : 500),
      overlap_(overlap < 0 ? 0 : overlap) {
    if (overlap_ >= chunkSize_) {
        overlap_ = chunkSize_ / 5;
    }
}

std::vector<TextChunk> TextChunker::chunk(const Document& doc) const {
    std::vector<TextChunk> out;
    auto paragraphs = splitParagraphs(doc.text);
    std::string current;

    auto emit = [&](const std::string& text) {
        auto trimmed = trimCopy(text);
        if (!trimmed.empty()) {
            out.push_back({doc.source, trimmed});
        }
    };

    for (const auto& para : paragraphs) {
        const size_t paraLen = utf8Length(para);
        if (paraLen > static_cast<size_t>(chunkSize_)) {
            if (!current.empty()) {
                emit(current);
                current.clear();
            }
            appendSlidingWindows(doc.source, para, out);
            continue;
        }

        if (current.empty()) {
            current = para;
            continue;
        }

        const size_t mergedLen = utf8Length(current) + 1 + paraLen;
        if (mergedLen <= static_cast<size_t>(chunkSize_)) {
            current.append("\n");
            current.append(para);
        } else {
            emit(current);
            current = para;
        }
    }
    if (!current.empty()) {
        emit(current);
    }
    return out;
}

void TextChunker::appendSlidingWindows(const std::string& source,
                                       const std::string& paragraph,
                                       std::vector<TextChunk>& out) const {
    const size_t total = utf8Length(paragraph);
    if (total == 0) {
        return;
    }
    const size_t step = static_cast<size_t>(chunkSize_ - overlap_);
    size_t start = 0;
    while (start < total) {
        auto piece = utf8Substring(paragraph, start, static_cast<size_t>(chunkSize_));
        auto trimmed = trimCopy(piece);
        if (!trimmed.empty()) {
            out.push_back({source, trimmed});
        }
        if (start + static_cast<size_t>(chunkSize_) >= total) {
            break;
        }
        start += (step == 0 ? 1 : step);
    }
}

}  // namespace rag
