#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <regex>
#include <fstream>
#include <sstream>
#include <iostream>
#include "../../../../HttpServer/include/utils/JsonUtil.h"  


struct AITool {
    std::string name;
    std::unordered_map<std::string, std::string> params;
    std::string desc;
};


struct AIToolCall {
    std::string toolName;
    json args;
    bool isToolCall = false;
};


class AIConfig {
public:
    bool loadFromFile(const std::string& path);
    std::string buildPrompt(const std::string& userInput) const;
    AIToolCall parseAIResponse(const std::string& response) const;
    std::string buildFollowUpPrompt(const std::string& userInput, const json& toolHistory, bool forceAnswer) const;

private:
    std::string promptTemplate_;
    std::vector<AITool> tools_;

    std::string buildToolList() const;
};
