#pragma once

#include <cstdlib>
#include <string>

inline std::string envOr(const char* key, const char* fallback) {
    const char* value = std::getenv(key);
    if (value && *value) {
        return std::string(value);
    }
    return std::string(fallback);
}

inline int envOrInt(const char* key, int fallback) {
    const char* value = std::getenv(key);
    if (!value || !*value) {
        return fallback;
    }
    return std::atoi(value);
}
