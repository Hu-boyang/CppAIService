#pragma once

#include "DocumentLoader.h"

#include <string>
#include <vector>

namespace rag {

struct TextChunk {
    std::string source;
    std::string text;
};

class TextChunker {
public:
    TextChunker(int chunkSize, int overlap);

    std::vector<TextChunk> chunk(const Document& doc) const;

private:
    void appendSlidingWindows(const std::string& source,
                              const std::string& paragraph,
                              std::vector<TextChunk>& out) const;

    int chunkSize_;
    int overlap_;
};

}  // namespace rag
