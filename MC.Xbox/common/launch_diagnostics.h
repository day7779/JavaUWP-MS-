#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace launchdiag {

inline std::string DiagnosticMinecraftAppArg(
    const std::vector<std::string>& args,
    size_t index) {
    constexpr char prefix[] = "--accessToken=";
    if (index > 0 && args[index - 1] == "--accessToken") return "<redacted>";
    if (args[index].compare(0, sizeof(prefix) - 1, prefix) == 0) return std::string(prefix) + "<redacted>";
    return args[index];
}

}
