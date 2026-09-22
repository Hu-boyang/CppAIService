#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <curl/curl.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <chrono>

#include "../../../../HttpServer/include/utils/JsonUtil.h"
#include "base64.h"

class AISpeechProcessor {
public:
    AISpeechProcessor(const std::string& clientId,
                      const std::string& clientSecret,
                      const std::string& cuid = "RZjSQGzNaA8EFWf6rvuHEKDh9i4XJIV9");

    std::string recognize(const std::string& speechData,
                          const std::string& format = "pcm",
                          int rate = 16000,
                          int channel = 1);

    // Returns mp3 bytes from Baidu short-text TTS.
    std::vector<uint8_t> synthesize(const std::string& text,
                                    const std::string& lang = "zh",
                                    int speed = 5,
                                    int pitch = 5,
                                    int volume = 5);

    void warmup();

private:
    std::string client_id_;
    std::string client_secret_;
    std::string cuid_;

    std::string ensureToken();
    void invalidateToken();
    std::string fetchAccessToken();
    std::vector<uint8_t> synthesizeChunk(const std::string& text, const std::string& lang,
                                         int speed, int pitch, int volume);

    static void applyCurlDefaults(CURL* curl, long timeoutSec);
    static std::string stripForTts(const std::string& text);
};
