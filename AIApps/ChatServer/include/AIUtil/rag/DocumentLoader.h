#pragma once

#include <string>
#include <vector>

namespace rag {

struct Document {
    std::string source;
    std::string text;
};

class DocumentLoader {
public:
    static std::vector<Document> loadDirectory(const std::string& dir);
};

}  // namespace rag
