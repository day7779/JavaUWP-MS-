#include "http_client.h"

#include "launcher_common.h"

#include <cctype>
#include <iomanip>
#include <sstream>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

std::string ExtractJsonStringValue(const std::string& content, const char* key) {
    if (!key || !*key) return {};
    const std::string needle = std::string("\"") + key + "\"";
    const size_t keyPos = content.find(needle);
    if (keyPos == std::string::npos) return {};

    size_t colonPos = content.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos) return {};
    size_t valueStart = content.find('"', colonPos + 1);
    if (valueStart == std::string::npos) return {};
    ++valueStart;

    std::string value;
    for (size_t i = valueStart; i < content.size(); ++i) {
        const char c = content[i];
        if (c == '\\') {
            if (i + 1 < content.size()) {
                value.push_back(content[i + 1]);
                ++i;
            }
            continue;
        }
        if (c == '"') {
            return value;
        }
        value.push_back(c);
    }

    return {};
}

int ExtractJsonIntValue(const std::string& content, const char* key, int fallback) {
    if (!key || !*key) return fallback;
    const std::string needle = std::string("\"") + key + "\"";
    const size_t keyPos = content.find(needle);
    if (keyPos == std::string::npos) return fallback;

    size_t pos = content.find(':', keyPos + needle.size());
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < content.size() && isspace(static_cast<unsigned char>(content[pos]))) {
        ++pos;
    }

    const size_t start = pos;
    if (pos < content.size() && content[pos] == '-') {
        ++pos;
    }
    while (pos < content.size() && isdigit(static_cast<unsigned char>(content[pos]))) {
        ++pos;
    }
    if (pos == start) return fallback;

    try {
        return std::stoi(content.substr(start, pos - start));
    } catch (...) {
        return fallback;
    }
}

std::string JsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
        case '\\': result += "\\\\"; break;
        case '"':  result += "\\\""; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[7] = {};
                sprintf_s(buf, "\\u%04x", c);
                result += buf;
            } else {
                result.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return result;
}

std::string FormUrlEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << static_cast<char>(c);
        } else if (c == ' ') {
            encoded << '+';
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return encoded.str();
}

std::string MakeFormBody(std::initializer_list<std::pair<std::string, std::string>> fields) {
    std::string body;
    bool first = true;
    for (const auto& field : fields) {
        if (!first) body += '&';
        first = false;
        body += FormUrlEncode(field.first);
        body += '=';
        body += FormUrlEncode(field.second);
    }
    return body;
}

std::string NormalizeMinecraftUuid(const std::string& value) {
    std::string compact;
    compact.reserve(value.size());
    for (char c : value) {
        if (c != '-') compact.push_back(c);
    }
    if (compact.size() != 32) {
        return value;
    }
    return compact.substr(0, 8) + "-" +
        compact.substr(8, 4) + "-" +
        compact.substr(12, 4) + "-" +
        compact.substr(16, 4) + "-" +
        compact.substr(20, 12);
}

HttpResult HttpPostString(const wchar_t* url, const std::string& body, const wchar_t* mediaType) {
    HttpResult result;
    try {
        using namespace winrt::Windows::Foundation;
        using namespace winrt::Windows::Storage::Streams;
        using namespace winrt::Windows::Web::Http;

        HttpClient client;
        HttpStringContent content(winrt::to_hstring(body), UnicodeEncoding::Utf8, mediaType);
        HttpResponseMessage response = client.PostAsync(winrt::Windows::Foundation::Uri(url), content).get();
        result.status = static_cast<int>(response.StatusCode());
        result.body = winrt::to_string(response.Content().ReadAsStringAsync().get());
    } catch (const winrt::hresult_error& ex) {
        WriteLogF(L"HTTP POST failed url=%s hr=0x%08X msg=%s",
            url, static_cast<unsigned int>(ex.code()), ex.message().c_str());
    }
    return result;
}

HttpResult HttpGetBearer(const wchar_t* url, const std::string& token) {
    HttpResult result;
    try {
        using namespace winrt::Windows::Foundation;
        using namespace winrt::Windows::Web::Http;
        using namespace winrt::Windows::Web::Http::Headers;

        HttpClient client;
        HttpRequestMessage request(HttpMethod::Get(), winrt::Windows::Foundation::Uri(url));
        request.Headers().Authorization(HttpCredentialsHeaderValue(L"Bearer", winrt::to_hstring(token)));
        HttpResponseMessage response = client.SendRequestAsync(request).get();
        result.status = static_cast<int>(response.StatusCode());
        result.body = winrt::to_string(response.Content().ReadAsStringAsync().get());
    } catch (const winrt::hresult_error& ex) {
        WriteLogF(L"HTTP GET failed url=%s hr=0x%08X msg=%s",
            url, static_cast<unsigned int>(ex.code()), ex.message().c_str());
    }
    return result;
}

HttpResult HttpGetString(const wchar_t* url) {
    HttpResult result;
    try {
        using namespace winrt::Windows::Foundation;
        using namespace winrt::Windows::Web::Http;

        HttpClient client;
        HttpRequestMessage request(HttpMethod::Get(), winrt::Windows::Foundation::Uri(url));
        request.Headers().UserAgent().ParseAdd(L"BanditVault-BanditLauncher/1.0");
        HttpResponseMessage response = client.SendRequestAsync(request).get();
        result.status = static_cast<int>(response.StatusCode());
        result.body = winrt::to_string(response.Content().ReadAsStringAsync().get());
    } catch (const winrt::hresult_error& ex) {
        WriteLogF(L"HTTP GET failed url=%s hr=0x%08X msg=%s",
            url, static_cast<unsigned int>(ex.code()), ex.message().c_str());
    }
    return result;
}
