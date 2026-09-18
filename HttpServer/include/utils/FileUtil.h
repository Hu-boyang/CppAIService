#pragma once

#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <muduo/base/Logging.h>

class FileUtil
{
public:
    FileUtil(std::string filePath)
        : filePath_(filePath)
        , file_(filePath, std::ios::binary) // 打开文件，二进制模式
    {}

    ~FileUtil()
    {
        if (file_.is_open())
        {
            file_.close();
        }
    }

    // 判断是否是有效路径
    bool isValid() const
    { return file_.is_open(); }

    static void setDefaultFile(std::string path)
    {
        defaultFilePath() = std::move(path);
    }

    // 重置打开默认文件（例如 404 页面）
    void resetDefaultFile()
    {
        file_.close();
        file_.clear();
        const std::string& path = defaultFilePath();
        if (path.empty())
        {
            LOG_ERROR << "Default file path is not configured";
            return;
        }
        filePath_ = path;
        file_.open(filePath_, std::ios::binary);
    }

    uint64_t size()
    {
        if (!file_.is_open())
        {
            return 0;
        }
        file_.seekg(0, std::ios::end); // 定位到文件末尾
        auto fileSize = file_.tellg();
        file_.seekg(0, std::ios::beg); // 返回到文件开头
        if (fileSize < 0)
        {
            return 0;
        }
        return static_cast<uint64_t>(fileSize);
    }

    void readFile(std::vector<char>& buffer)
    {
        if (!file_.is_open() || buffer.empty())
        {
            LOG_ERROR << "File read failed";
            return;
        }
        if (file_.read(buffer.data(), static_cast<std::streamsize>(buffer.size())))
        {
            LOG_INFO << "File content load into memory (" << buffer.size() << " bytes)";
        }
        else
        {
            LOG_ERROR << "File read failed";
        }
    }

private:
    static std::string& defaultFilePath()
    {
        static std::string path;
        return path;
    }

    std::string     filePath_;
    std::ifstream   file_;
};
