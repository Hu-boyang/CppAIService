#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rag {

inline bool isCjkCodepoint(uint32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF) ||
           (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0x20000 && cp <= 0x2A6DF);
}

inline std::vector<uint32_t> utf8Decode(const std::string& text) {
    std::vector<uint32_t> codepoints;
    codepoints.reserve(text.size());
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    const auto* end = p + text.size();
    while (p < end) {
        uint32_t cp = 0;
        int extra = 0;
        if (*p < 0x80) {
            cp = *p;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = *p & 0x1F;
            extra = 1;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p & 0x0F;
            extra = 2;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p & 0x07;
            extra = 3;
        } else {
            ++p;
            continue;
        }
        ++p;
        bool valid = true;
        for (int i = 0; i < extra; ++i) {
            if (p >= end || (*p & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (*p & 0x3F);
            ++p;
        }
        if (valid) {
            codepoints.push_back(cp);
        }
    }
    return codepoints;
}

inline std::string utf8Encode(const std::vector<uint32_t>& codepoints) {
    std::string out;
    out.reserve(codepoints.size() * 3);
    for (uint32_t cp : codepoints) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

inline size_t utf8Length(const std::string& text) {
    return utf8Decode(text).size();
}

inline std::string utf8Substring(const std::string& text, size_t start, size_t count) {
    auto cps = utf8Decode(text);
    if (start >= cps.size()) {
        return {};
    }
    size_t end = start + count;
    if (end > cps.size()) {
        end = cps.size();
    }
    return utf8Encode(std::vector<uint32_t>(cps.begin() + static_cast<std::ptrdiff_t>(start),
                                            cps.begin() + static_cast<std::ptrdiff_t>(end)));
}

inline std::string trimCopy(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

inline std::vector<std::string> tokenize(const std::string& text) {
    auto cps = utf8Decode(text);
    std::vector<std::string> tokens;
    std::string latin;
    std::vector<uint32_t> cjkRun;

    auto flushLatin = [&]() {
        if (!latin.empty()) {
            tokens.push_back(latin);
            latin.clear();
        }
    };
    auto flushCjk = [&]() {
        if (cjkRun.size() == 1) {
            tokens.push_back(utf8Encode({cjkRun[0]}));
        } else {
            for (size_t i = 0; i + 1 < cjkRun.size(); ++i) {
                tokens.push_back(utf8Encode({cjkRun[i], cjkRun[i + 1]}));
            }
        }
        cjkRun.clear();
    };

    for (uint32_t cp : cps) {
        if (cp < 128) {
            auto ch = static_cast<unsigned char>(cp);
            if (std::isalnum(ch)) {
                flushCjk();
                latin.push_back(static_cast<char>(std::tolower(ch)));
                continue;
            }
        }
        if (isCjkCodepoint(cp)) {
            flushLatin();
            cjkRun.push_back(cp);
            continue;
        }
        flushLatin();
        flushCjk();
    }
    flushLatin();
    flushCjk();
    return tokens;
}

}  // namespace rag
