#include "AIUtil/rag/DocumentLoader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace rag {
namespace {

std::string toLowerExt(std::string ext) {
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

std::string readFileUtf8(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

std::vector<Document> DocumentLoader::loadDirectory(const std::string& dir) {
    std::vector<Document> docs;
    std::error_code ec;
    std::filesystem::path root(dir);
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        std::cerr << "[RAG] knowledge directory not found: " << dir << std::endl;
        return docs;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec || !entry.is_regular_file(ec)) {
            continue;
        }
        auto ext = toLowerExt(entry.path().extension().string());
        if (ext != ".md" && ext != ".txt") {
            continue;
        }
        Document doc;
        doc.source = std::filesystem::relative(entry.path(), root, ec).generic_string();
        if (ec || doc.source.empty()) {
            doc.source = entry.path().filename().string();
        }
        doc.text = readFileUtf8(entry.path());
        if (doc.text.empty()) {
            continue;
        }
        docs.push_back(std::move(doc));
    }
    return docs;
}

}  // namespace rag
