#include "../include/AIUtil/AISpeechProcessor.h"

#include <stdexcept>
#include <sstream>
#include <iostream>
#include <mutex>
#include <cctype>
#include <cstdint>
#include <vector>

namespace {

struct TokenCache {
    std::mutex mutex;
    std::string clientId;
    std::string token;
    std::chrono::steady_clock::time_point expiry;
};

TokenCache& tokenCache() {
    static TokenCache cache;
    return cache;
}

bool isPermissionDenied(const std::string& message) {
    if (message.find("No permission") != std::string::npos) {
        return true;
    }
    return message.rfind("6:", 0) == 0 || message.find(": 6:") != std::string::npos;
}

std::mutex gShareMutexDns;
std::mutex gShareMutexSsl;
std::mutex gShareMutexConnect;

void shareLock(CURL*, curl_lock_data data, curl_lock_access, void*) {
    if (data == CURL_LOCK_DATA_DNS) {
        gShareMutexDns.lock();
    } else if (data == CURL_LOCK_DATA_SSL_SESSION) {
        gShareMutexSsl.lock();
    } else if (data == CURL_LOCK_DATA_CONNECT) {
        gShareMutexConnect.lock();
    }
}

void shareUnlock(CURL*, curl_lock_data data, void*) {
    if (data == CURL_LOCK_DATA_DNS) {
        gShareMutexDns.unlock();
    } else if (data == CURL_LOCK_DATA_SSL_SESSION) {
        gShareMutexSsl.unlock();
    } else if (data == CURL_LOCK_DATA_CONNECT) {
        gShareMutexConnect.unlock();
    }
}

CURLSH* ttsShare() {
    static CURLSH* share = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        share = curl_share_init();
        curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
        curl_share_setopt(share, CURLSHOPT_LOCKFUNC, shareLock);
        curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, shareUnlock);
    });
    return share;
}

size_t onWriteData(void* buffer, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(buffer), size * nmemb);
    return size * nmemb;
}

size_t onWriteBytes(void* buffer, size_t size, size_t nmemb, void* userp) {
    auto* out = static_cast<std::vector<uint8_t>*>(userp);
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    out->insert(out->end(), bytes, bytes + size * nmemb);
    return size * nmemb;
}

std::string curlEscape(CURL* curl, const std::string& value) {
    char* escaped = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    std::string out = escaped ? escaped : "";
    if (escaped) {
        curl_free(escaped);
    }
    return out;
}

// 判断是否是 JSON 格式
bool isJsonBody(const std::vector<uint8_t>& body) {
    size_t i = 0;
    while (i < body.size() && (body[i] == ' ' || body[i] == '\n' || body[i] == '\r' || body[i] == '\t')) {
        ++i;
    }
    return i < body.size() && (body[i] == '{' || body[i] == '[');
}

}  // namespace

AISpeechProcessor::AISpeechProcessor(const std::string& clientId,
                                     const std::string& clientSecret,
                                     const std::string& cuid)
    : client_id_(clientId), client_secret_(clientSecret), cuid_(cuid) {}

void AISpeechProcessor::applyCurlDefaults(CURL* curl, long timeoutSec) {
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_SHARE, ttsShare());
}

// 设置 tokenCache 缓存
std::string AISpeechProcessor::ensureToken() {
    auto& cache = tokenCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    const auto now = std::chrono::steady_clock::now();
    if (cache.clientId == client_id_ && !cache.token.empty() && now < cache.expiry) {
        return cache.token;
    }
    std::string token = fetchAccessToken();
    if (token.empty()) {
        throw std::runtime_error("获取百度 access_token 失败");
    }
    cache.clientId = client_id_;
    cache.token = token;
    cache.expiry = now + std::chrono::hours(24 * 25);
    return token;
}

void AISpeechProcessor::invalidateToken() {
    auto& cache = tokenCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    cache.token.clear();
}

void AISpeechProcessor::warmup() {
    ensureToken();
    try {
        (void)synthesizeChunk("你好", "zh", 5, 5, 5);
        std::cout << "Baidu TTS short-text primed" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "TTS warmup synth skipped: " << e.what() << std::endl;
    }
}

std::string AISpeechProcessor::fetchAccessToken() {
    std::string result;
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "";
    }

    curl_easy_setopt(curl, CURLOPT_URL, "https://aip.baidubce.com/oauth/2.0/token");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    applyCurlDefaults(curl, 20);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    std::string data = "grant_type=client_credentials&client_id=" + client_id_
                       + "&client_secret=" + client_secret_;
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onWriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        return "";
    }

    try {
        auto j = json::parse(result);
        if (j.contains("access_token") && j["access_token"].is_string()) {
            return j["access_token"].get<std::string>();
        }
    } catch (...) {
    }
    return "";
}

std::string AISpeechProcessor::stripForTts(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool inFence = false;
    for (size_t i = 0; i < text.size();) {
        if (i + 2 < text.size() && text.compare(i, 3, "```") == 0) {
            inFence = !inFence;
            i += 3;
            out.push_back(' ');
            continue;
        }
        if (inFence) {
            ++i;
            continue;
        }
        if (text.compare(i, 8, "https://") == 0 || text.compare(i, 7, "http://") == 0) {
            while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            continue;
        }
        const char c = text[i];
        if (c == '#' || c == '*' || c == '_' || c == '`' || c == '>' || c == '|') {
            ++i;
            continue;
        }
        out.push_back(c);
        ++i;
    }
    std::string compact;
    compact.reserve(out.size());
    bool space = false;
    for (char c : out) {
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
        if (c == ' ') {
            if (!space) {
                compact.push_back(' ');
            }
            space = true;
        } else {
            compact.push_back(c);
            space = false;
        }
    }
    while (!compact.empty() && compact.front() == ' ') {
        compact.erase(compact.begin());
    }
    while (!compact.empty() && compact.back() == ' ') {
        compact.pop_back();
    }
    return compact;
}

std::vector<uint8_t> AISpeechProcessor::synthesizeChunk(const std::string& text, const std::string& lang,
                                                        int speed, int pitch, int volume) {
    const std::string token = ensureToken();
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }

    std::vector<uint8_t> response;
    curl_easy_setopt(curl, CURLOPT_URL, "https://tsn.baidu.com/text2audio");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    applyCurlDefaults(curl, 12);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // 百度语音需要双重编码
    const std::string tex = curlEscape(curl, curlEscape(curl, text));
    std::ostringstream body;
    body << "tex=" << tex
         << "&tok=" << curlEscape(curl, token)
         << "&cuid=" << curlEscape(curl, cuid_)
         << "&ctp=1"
         << "&lan=" << curlEscape(curl, lang)
         << "&spd=" << speed
         << "&pit=" << pitch
         << "&vol=" << volume
         << "&per=0"
         << "&aue=3";
    const std::string payload = body.str();

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onWriteBytes);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("TTS 请求失败: ") + curl_easy_strerror(res));
    }

    // 如果成功返回的是 mp3 不成功大概率返回 json 格式
    if (isJsonBody(response)) {
        int errNo = 0;
        std::string message = "短文本 TTS 失败";
        try {
            const std::string errText(reinterpret_cast<const char*>(response.data()),
                                      response.size());
            auto err = json::parse(errText);
            if (err.contains("err_no") && err["err_no"].is_number_integer()) {
                errNo = err["err_no"].get<int>();
            }
            if (err.contains("err_msg") && err["err_msg"].is_string()) {
                message = err["err_msg"].get<std::string>();
            } else if (err.contains("error_msg") && err["error_msg"].is_string()) {
                message = err["error_msg"].get<std::string>();
            }
            if (errNo != 0) {
                message = std::to_string(errNo) + ": " + message;
            }
        } catch (...) {
        }
        throw std::runtime_error(message);
    }
    if (httpCode >= 400 || response.empty()) {
        throw std::runtime_error("短文本 TTS 返回空音频");
    }
    return response;
}

std::vector<uint8_t> AISpeechProcessor::synthesize(const std::string& text,
                                                   const std::string& lang,
                                                   int speed,
                                                   int pitch,
                                                   int volume) {
    if (text.empty()) {
        throw std::runtime_error("合成文本为空");
    }

    const auto started = std::chrono::steady_clock::now();
    std::string plain = stripForTts(text);
    if (plain.empty()) {
        plain = text;
    }

    auto finishLog = [&](const std::vector<uint8_t>& audio) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started)
                            .count();
        std::cout << "TTS bytes=" << audio.size() << " ms=" << ms << std::endl;
    };

    std::vector<uint8_t> audio;
    try {
        audio = synthesizeChunk(plain, lang, speed, pitch, volume);
    } catch (const std::exception& firstError) {
        const std::string message = firstError.what();
        const bool retryWithNewToken =
            isPermissionDenied(message)
            || message.find("token") != std::string::npos
            || message.find("502") != std::string::npos;
        if (!retryWithNewToken) {
            throw;
        }

        invalidateToken();
        try {
            audio = synthesizeChunk(plain, lang, speed, pitch, volume);
        } catch (const std::exception& retryError) {
            if (isPermissionDenied(retryError.what())) {
                throw std::runtime_error(
                    "百度短文本合成仍未授权，请确认 docker/app.env 中的 API Key "
                    "属于已开通短文本语音合成的应用");
            }
            throw;
        }
    }
    if (audio.empty()) {
        throw std::runtime_error("短文本 TTS 未返回音频");
    }
    finishLog(audio);
    return audio;
}

// 识别音频数据
std::string AISpeechProcessor::recognize(const std::string& speechData,
                                         const std::string& format,
                                         int rate,
                                         int channel) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "";
    }

    std::string result;
    curl_easy_setopt(curl, CURLOPT_URL, "https://vop.baidu.com/server_api");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    applyCurlDefaults(curl, 30);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    json body;
    body["format"] = format;
    body["rate"] = rate;
    body["channel"] = channel;
    body["cuid"] = cuid_;
    body["token"] = ensureToken();
    body["len"] = static_cast<int>(speechData.size());
    body["speech"] = speechData;

    std::string data = body.dump();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onWriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        return "";
    }

    try {
        json root = json::parse(result);
        if (root.contains("result") && root["result"].is_array() && !root["result"].empty()) {
            if (root["result"][0].is_string()) {
                return root["result"][0].get<std::string>();
            }
        }
    } catch (...) {
        std::cout << "Parse error in recognize response: " << result << std::endl;
    }
    std::cout << "Recognize failed, response: " << result << std::endl;
    return "";
}
