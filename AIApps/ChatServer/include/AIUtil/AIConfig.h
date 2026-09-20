#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <regex>
#include <fstream>
#include <sstream>
#include <iostream>
#include "../../../../HttpServer/include/utils/JsonUtil.h"  

/*
    params 是工具的参数说明书，不是某次调用时的真实入参。形态就是「参数名 → 一段中文说明」，从 config.json 读进来。
 */
struct AITool {
    std::string name;
    std::unordered_map<std::string, std::string> params;
    std::string desc;
};


struct AIToolCall {
    std::string toolName;
    json args;
};

class AIConfig {
public:
    bool loadFromFile(const std::string& path);
    std::string buildPrompt(const std::string& userInput) const;
    std::vector<AIToolCall> parseToolCalls(const std::string& response) const;
    std::string buildFollowUpPrompt(const std::string& userInput, const json& toolHistory, bool forceAnswer) const;

private:
    std::string promptTemplate_;
    std::vector<AITool> tools_;

    std::string buildToolList() const;
};
