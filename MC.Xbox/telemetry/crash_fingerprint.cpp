#include "crash_fingerprint.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include <cstdio>

namespace crashfp {
namespace {

constexpr char kTokenDelimiters[] = " \t\r\n\"'([,=";

std::string LowerAscii(const std::string& value) {
    std::string out = value;
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

bool IsAlnum(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

bool IsDigit(char ch) {
    return ch >= '0' && ch <= '9';
}

void CapChars(std::string& value, size_t limit) {
    if (value.size() <= limit) return;
    value.resize(limit);
    // keep truncated json valid utf8
    while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0x80) != 0) {
        value.pop_back();
    }
}

std::string TrimAscii(std::string value) {
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' ||
        value[start] == '\r' || value[start] == '\n')) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
        value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string StripTrailingPunctuation(std::string value) {
    while (!value.empty()) {
        const char back = value.back();
        if (back == '.' || back == ',' || back == ';' || back == ')' ||
            back == ']' || back == '\'' || back == '"' || back == '!') {
            value.pop_back();
            continue;
        }
        break;
    }
    return value;
}

std::string TokenAt(const std::string& value, size_t start) {
    if (start >= value.size()) return std::string();
    const size_t end = value.find_first_of(" \t\r\n", start);
    return StripTrailingPunctuation(value.substr(start, (end == std::string::npos) ? std::string::npos : end - start));
}

std::string FirstQuoted(const std::string& value) {
    const char quotes[] = { '\'', '"' };
    for (const char quote : quotes) {
        const size_t open = value.find(quote);
        if (open == std::string::npos) continue;
        const size_t close = value.find(quote, open + 1);
        if (close == std::string::npos || close == open + 1) continue;
        return value.substr(open + 1, close - open - 1);
    }
    return std::string();
}

}

std::string Sha256Hex16(const std::string& value) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD dataLength = 0;
    std::vector<unsigned char> hashObject;
    unsigned char digest[32] = {};
    std::string out;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return out;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
        sizeof(objectLength), &dataLength, 0) == 0 && objectLength != 0) {
        hashObject.resize(objectLength);
        if (BCryptCreateHash(alg, &hash, hashObject.data(), objectLength, nullptr, 0, 0) == 0) {
            if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())),
                static_cast<ULONG>(value.size()), 0) == 0 &&
                BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
                char hex[17] = {};
                for (int i = 0; i < 8; ++i) {
                    sprintf_s(hex + (i * 2), 3, "%02x", digest[i]);
                }
                out.assign(hex, 16);
            }
            BCryptDestroyHash(hash);
        }
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

std::string ScrubPaths(std::string value) {
    for (;;) {
        const std::string lower = LowerAscii(value);
        size_t at = lower.find(":\\users\\");
        if (at == std::string::npos) at = lower.find(":/users/");
        if (at == std::string::npos) break;

        const size_t driveStart = (at == 0) ? 0 : at - 1;
        const size_t nameStart = at + 8;
        size_t nameEnd = value.find_first_of("\\/", nameStart);
        if (nameEnd == std::string::npos) nameEnd = value.size();
        value.replace(driveStart, nameEnd - driveStart, "<user>");
    }

    for (;;) {
        const std::string lower = LowerAscii(value);
        const size_t at = lower.find("localstate");
        if (at == std::string::npos) break;

        size_t start = value.find_last_of(kTokenDelimiters, at);
        start = (start == std::string::npos) ? 0 : start + 1;
        // the replacement cannot contain the search text
        value.replace(start, (at + 10) - start, "<pkg>");
    }

    return value;
}

std::string NormalizeFrame(std::string frame) {
    frame = TrimAscii(frame);
    if (frame.compare(0, 3, "at ") == 0) frame.erase(0, 3);

    const size_t suffix = frame.find(" ~[");
    if (suffix != std::string::npos) frame.erase(suffix);
    frame = TrimAscii(frame);

    const size_t open = frame.rfind('(');
    const size_t close = frame.rfind(')');
    if (open != std::string::npos && close != std::string::npos && close > open) {
        const size_t colon = frame.rfind(':', close);
        if (colon != std::string::npos && colon > open) {
            frame.erase(colon, close - colon);
        }
    }

    // mixin ids change with mod load order
    for (size_t i = 0; i + 7 < frame.size(); ++i) {
        if (frame[i] != '$' || frame[i + 7] != '$') continue;
        bool allAlnum = true;
        for (size_t j = i + 1; j < i + 7; ++j) {
            if (!IsAlnum(frame[j])) {
                allAlnum = false;
                break;
            }
        }
        if (!allAlnum) continue;
        frame.erase(i + 1, 7);
    }

    for (size_t i = 0; i < frame.size(); ++i) {
        if (frame[i] != '$') continue;
        size_t end = i + 1;
        while (end < frame.size() && IsDigit(frame[end])) ++end;
        if (end > i + 1) frame.erase(i + 1, end - (i + 1));
    }

    return frame;
}

CrashDetail ExtractDetail(
    const std::string& exceptionChain,
    const std::string& message,
    const std::vector<std::string>& frames) {
    CrashDetail detail;
    (void)frames;

    const std::string lowerChain = LowerAscii(exceptionChain);
    const std::string lowerMessage = LowerAscii(message);

    const bool nativeLink =
        lowerChain.find("unsatisfiedlinkerror") != std::string::npos ||
        (lowerChain.find("nosuchmethoderror") != std::string::npos &&
            (lowerMessage.find("lwjgl") != std::string::npos ||
             lowerMessage.find("glfw") != std::string::npos));

    if (nativeLink) {
        detail.kind = "native_link";
        detail.symbol = FirstQuoted(message);
        if (detail.symbol.empty()) detail.symbol = TrimAscii(message);
        CapChars(detail.symbol, kMaxDetailChars);
        return detail;
    }

    const bool mixin =
        lowerChain.find("mixin") != std::string::npos ||
        lowerChain.find("injection") != std::string::npos;
    if (!mixin) return detail;

    detail.kind = "mixin";

    const size_t inAt = message.rfind(" in ");
    if (inAt != std::string::npos) {
        const std::string candidate = TokenAt(message, inAt + 4);
        if (candidate.find('/') != std::string::npos || candidate.find('.') != std::string::npos) {
            detail.targetClass = candidate;
        }
    }

    const size_t methodAt = lowerMessage.find("target method ");
    if (methodAt != std::string::npos) {
        detail.targetMethod = TokenAt(message, methodAt + 14);
    }
    if (detail.targetMethod.empty()) {
        detail.targetMethod = FirstQuoted(message);
    }

    const size_t descOpen = message.find('(');
    if (descOpen != std::string::npos) {
        const size_t descClose = message.find(')', descOpen + 1);
        if (descClose != std::string::npos) {
            bool descriptorLike = true;
            for (size_t i = descOpen + 1; i < descClose; ++i) {
                const char ch = message[i];
                if (IsAlnum(ch) || ch == '/' || ch == ';' || ch == '[' || ch == '$' || ch == '_') continue;
                descriptorLike = false;
                break;
            }
            if (descriptorLike) {
                size_t end = descClose + 1;
                while (end < message.size() &&
                    (IsAlnum(message[end]) || message[end] == '/' || message[end] == ';' ||
                     message[end] == '[' || message[end] == '$' || message[end] == '_')) {
                    ++end;
                }
                detail.descriptor = message.substr(descOpen, end - descOpen);
            }
        }
    }

    const size_t jsonAt = lowerMessage.find(".json");
    if (jsonAt != std::string::npos) {
        size_t start = message.find_last_of(kTokenDelimiters, jsonAt);
        start = (start == std::string::npos) ? 0 : start + 1;
        const std::string token = message.substr(start, jsonAt - start);
        const std::string lowerToken = LowerAscii(token);
        const size_t mixinsAt = lowerToken.find("mixins");
        if (mixinsAt == 0) {
            std::string rest = token.substr(6);
            if (!rest.empty() && rest.front() == '.') rest.erase(0, 1);
            detail.owningMod = rest;
        } else if (mixinsAt != std::string::npos) {
            std::string head = token.substr(0, mixinsAt);
            while (!head.empty() && head.back() == '.') head.pop_back();
            detail.owningMod = head;
        }
    }

    CapChars(detail.targetClass, kMaxDetailChars);
    CapChars(detail.targetMethod, kMaxDetailChars);
    CapChars(detail.descriptor, kMaxDetailChars);
    CapChars(detail.owningMod, kMaxDetailChars);
    return detail;
}

std::string DetailDiscriminator(const CrashDetail& detail) {
    std::string value;
    if (detail.kind == "mixin") {
        value = detail.owningMod + "|" + detail.targetClass + "|" + detail.targetMethod;
    } else if (detail.kind == "native_link") {
        value = detail.symbol;
    }

    for (const char ch : value) {
        if (ch != '|') return value;
    }
    return std::string();
}

std::string Fingerprint(
    const std::string& outerClass,
    const std::string& rootClass,
    const std::vector<std::string>& frames,
    const std::string& discriminator) {
    if (outerClass.empty() && frames.empty()) return std::string();

    std::string input = outerClass;
    if (!rootClass.empty() && rootClass != outerClass) {
        input += ">";
        input += rootClass;
    }
    input += "|";
    for (const std::string& frame : frames) {
        input += frame;
        input += "\n";
    }
    if (!discriminator.empty()) {
        input += "|";
        input += discriminator;
    }
    return Sha256Hex16(input);
}

JavaCrash Build(
    const std::string& outerClass,
    const std::string& rootClass,
    const std::string& rawMessage,
    const std::vector<std::string>& rawFrames) {
    JavaCrash crash;

    crash.exception = outerClass;
    if (!rootClass.empty() && rootClass != outerClass) {
        crash.exception += " -> ";
        crash.exception += rootClass;
    }

    crash.message = ScrubPaths(TrimAscii(rawMessage));
    CapChars(crash.message, kMaxMessageChars);

    for (const std::string& raw : rawFrames) {
        std::string frame = NormalizeFrame(raw);
        if (frame.empty()) continue;
        crash.frames.push_back(frame);
        if (crash.frames.size() >= kMaxFrames) break;
    }

    crash.detail = ExtractDetail(crash.exception, crash.message, crash.frames);
    crash.fingerprint = Fingerprint(outerClass, rootClass, crash.frames, DetailDiscriminator(crash.detail));
    return crash;
}

}
