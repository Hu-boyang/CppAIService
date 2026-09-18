#pragma once

#include "EnvUtil.h"

#include <string>

inline std::string chatResourceDir()
{
    return envOr("CHAT_RESOURCE_DIR", "../AIApps/ChatServer/resource");
}

inline std::string chatResourceFile(const std::string& name)
{
    const std::string dir = chatResourceDir();
    if (dir.empty())
    {
        return name;
    }
    if (dir.back() == '/')
    {
        return dir + name;
    }
    return dir + "/" + name;
}
