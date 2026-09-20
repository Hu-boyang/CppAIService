#pragma once

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

// 键存在且值为字符串才写入 out，避免 operator[] 隐式转换和 get 混用。
inline bool jsonGetString(const json& obj, const char* key, std::string& out)
{
    if (!obj.contains(key) || !obj[key].is_string()) {
        return false;
    }
    out = obj[key].get<std::string>();
    return true;
}
