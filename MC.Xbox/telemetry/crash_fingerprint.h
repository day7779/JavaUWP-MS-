#pragma once

#include <string>
#include <vector>

namespace crashfp {

constexpr size_t kMaxFrames = 10;
constexpr size_t kMaxMessageChars = 1000;
constexpr size_t kMaxDetailChars = 200;

struct CrashDetail {
    std::string kind;
    std::string targetClass;
    std::string targetMethod;
    std::string descriptor;
    std::string owningMod;
    std::string symbol;

    bool empty() const { return kind.empty(); }
};

struct JavaCrash {
    std::string exception;
    std::string message;
    std::vector<std::string> frames;
    std::string fingerprint;
    CrashDetail detail;
    int heapMaxMb = 0;
    int heapAtCrashMb = 0;

    bool valid() const { return !fingerprint.empty(); }
};

std::string Sha256Hex16(const std::string& value);

std::string ScrubPaths(std::string value);
std::string NormalizeFrame(std::string frame);

CrashDetail ExtractDetail(
    const std::string& exceptionChain,
    const std::string& message,
    const std::vector<std::string>& frames);

std::string DetailDiscriminator(const CrashDetail& detail);

std::string Fingerprint(
    const std::string& outerClass,
    const std::string& rootClass,
    const std::vector<std::string>& frames,
    const std::string& discriminator);

JavaCrash Build(
    const std::string& outerClass,
    const std::string& rootClass,
    const std::string& rawMessage,
    const std::vector<std::string>& rawFrames);

}
