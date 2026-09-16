#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <timeapi.h>
using SocketHandle = SOCKET;
using SockLen = int;
static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SocketHandle = int;
using SockLen = socklen_t;
static constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace {

constexpr uint16_t kInputPort = 42731;
constexpr uint16_t kStatusPort = 42732;
constexpr uint16_t kMicPort = 42733;
constexpr uint16_t kGameAudioPort = 42734;
constexpr float kTargetWidth = 1920.0f;
constexpr float kTargetHeight = 1080.0f;
constexpr double kSendIntervalSeconds = 1.0 / 240.0;
constexpr double kHeldButtonRefreshSeconds = 1.0 / 20.0;
constexpr double kConnectProbeSeconds = 0.25;
constexpr double kConnectTimeoutSeconds = 3.0;
constexpr double kRenderIntervalSeconds = 1.0 / 30.0;
constexpr double kButtonHoldSeconds = 1.0;
constexpr double kMicHoldSeconds = 0.5;
constexpr double kTouchScrollRepeatSeconds = 1.0 / 12.0;
constexpr double kLinkLostSeconds = 4.0;
constexpr double kSavedIpProbeSeconds = 0.6;
constexpr double kSweepListenSeconds = 0.7;
constexpr double kSweepPauseSeconds = 1.5;
constexpr float kMenuCoordinateScale = 0.5f;
constexpr float kDebugGlyphWidth = 8.0f;
constexpr float kDebugGlyphHeight = 8.0f;
constexpr float kTouchScrollStep = 1.0f;

constexpr int kMicSampleRate = 48000;
constexpr int kMicFrameSamples = 480;
constexpr int kMicFrameBytes = kMicFrameSamples * 2;
constexpr int kMicHeaderBytes = 16;
constexpr double kGameAudioKeepaliveSeconds = 1.5;
constexpr double kGameAudioQueueCapSeconds = 0.25;
constexpr const char* kDefaultMicName = "System default";
constexpr const char* kReadyPrefix = "javauwp_glfw_mouse:ready";

#ifdef BANDIT_MOUSE_RELAY_TOUCH
constexpr bool kTouchLayout = true;
#else
constexpr bool kTouchLayout = false;
#endif

enum class RelayMode {
    Gameplay,
    Menu,
};

enum class RelaySet {
    Mouse,
    Mic,
    Both,
};

enum class UiAction {
    None,
    Connect,
    CancelIp,
    ChangeIp,
    EnterIp,
    ToggleMode,
    ReleaseButtons,
    Resume,
    Quit,
    PickMouse,
    PickMic,
    PickBoth,
    ChangeSet,
    ToggleMic,
    ToggleGameAudio,
    OpenMicPicker,
    PickMicDevice,
    OpenMenu,
};

enum class MicPickReturn {
    Relay,
    Menu,
};

struct Button {
    SDL_FRect rect{};
    std::string label;
    UiAction action = UiAction::None;
    int mouseButtonIndex = -1;
    float scrollStep = 0.0f;
    std::string inputText;
    bool backspace = false;
    bool disabled = false;
    int listIndex = -1;
};

static float Clamp(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

static double NowSeconds() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

static void SleepUntil(double deadline) {
    for (;;) {
        const double remaining = deadline - NowSeconds();
        if (remaining <= 0.0) {
            return;
        }
        // sleep the bulk, spin the last ~1ms since SDL_Delay only resolves to the timer tick
        if (remaining > 0.002) {
            SDL_Delay(static_cast<Uint32>((remaining - 0.001) * 1000.0));
        } else {
            // an empty spin burns a core at 240hz with the thread at time critical
            std::this_thread::yield();
        }
    }
}

class PacketLog {
public:
    void Toggle() {
        if (enabled_) {
            Disable();
        } else {
            Enable();
        }
    }

    bool Enabled() const { return enabled_; }
    const std::string& Path() const { return path_; }

    void Enable() {
        if (enabled_) {
            return;
        }
        std::string dir;
        char* base = SDL_GetPrefPath("BanditVault", "BanditMouseRelay");
        if (base) {
            dir = base;
            SDL_free(base);
        }
        char stamp[32];
        std::time_t tt = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &tt);
#else
        localtime_r(&tt, &tmv);
#endif
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);
        path_ = dir + "relay-packets-" + stamp + ".log";
        out_.open(path_, std::ios::out | std::ios::trunc);
        if (!out_.is_open()) {
            path_.clear();
            return;
        }
        seqTx_ = 0;
        seqRx_ = 0;
        lastTx_ = -1.0;
        lastRx_ = -1.0;
        lastFlush_ = NowSeconds();
        enabled_ = true;
        out_ << "# Bandit Relay packet log\n";
        out_ << "# format: wall_clock mono_seconds dir seq dt_ms payload\n";
        out_ << "# dir TX = sent to Xbox, RX = status from Xbox, EV = event marker\n";
        out_.flush();
    }

    void Disable() {
        if (!enabled_) {
            return;
        }
        enabled_ = false;
        out_.flush();
        out_.close();
    }

    void Mark(const std::string& note) {
        if (!enabled_) {
            return;
        }
        char head[112];
        FormatHead(head, sizeof(head), "EV", 0, -1.0);
        out_ << head << note << '\n';
    }

    void LogTx(const std::string& packet, bool ok) {
        if (!enabled_) {
            return;
        }
        const double now = NowSeconds();
        const double dt = lastTx_ >= 0.0 ? (now - lastTx_) * 1000.0 : -1.0;
        lastTx_ = now;
        char head[112];
        FormatHead(head, sizeof(head), "TX", ++seqTx_, dt);
        out_ << head << (ok ? "" : "SENDFAIL ") << packet << '\n';
    }

    void LogRx(const std::string& packet) {
        if (!enabled_) {
            return;
        }
        const double now = NowSeconds();
        const double dt = lastRx_ >= 0.0 ? (now - lastRx_) * 1000.0 : -1.0;
        lastRx_ = now;
        char head[112];
        FormatHead(head, sizeof(head), "RX", ++seqRx_, dt);
        out_ << head << packet << '\n';
    }

    void MaybeFlush() {
        if (!enabled_) {
            return;
        }
        const double now = NowSeconds();
        if (now - lastFlush_ >= 0.25) {
            out_.flush();
            lastFlush_ = now;
        }
    }

private:
    void FormatHead(char* buffer, size_t size, const char* dir, long long seq, double dtMs) {
        const auto sysNow = std::chrono::system_clock::now();
        const std::time_t tt = std::chrono::system_clock::to_time_t(sysNow);
        const int millis = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(sysNow.time_since_epoch()).count() % 1000);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &tt);
#else
        localtime_r(&tt, &tmv);
#endif
        char wall[16];
        std::strftime(wall, sizeof(wall), "%H:%M:%S", &tmv);
        if (dtMs >= 0.0) {
            std::snprintf(buffer, size, "%s.%03d %.6f %s seq=%lld dt=%.2fms ",
                wall, millis, NowSeconds(), dir, seq, dtMs);
        } else {
            std::snprintf(buffer, size, "%s.%03d %.6f %s seq=%lld ",
                wall, millis, NowSeconds(), dir, seq);
        }
    }

    bool enabled_ = false;
    std::ofstream out_;
    std::string path_;
    long long seqTx_ = 0;
    long long seqRx_ = 0;
    double lastTx_ = -1.0;
    double lastRx_ = -1.0;
    double lastFlush_ = 0.0;
};

PacketLog g_packetLog;

static std::string ModeName(RelayMode mode) {
    return mode == RelayMode::Gameplay ? "GAMEPLAY" : "MENU";
}

static std::string SetName(RelaySet set) {
    switch (set) {
    case RelaySet::Mouse:
        return "Mouse";
    case RelaySet::Mic:
        return "Mic";
    case RelaySet::Both:
        return "Mouse + Mic";
    }
    return "Mouse";
}

static bool IsValidIpv4(const std::string& text) {
    int octets = 0;
    size_t start = 0;

    while (start <= text.size()) {
        const size_t end = text.find('.', start);
        const size_t stop = (end == std::string::npos) ? text.size() : end;
        if (stop == start || stop - start > 3) {
            return false;
        }

        int value = 0;
        for (size_t i = start; i < stop; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return false;
            }
            value = (value * 10) + (text[i] - '0');
        }
        if (value > 255) {
            return false;
        }

        ++octets;
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return octets == 4;
}

static std::string PrefPath(const char* name) {
    char* base = SDL_GetPrefPath("BanditVault", "BanditMouseRelay");
    if (!base) {
        return std::string();
    }
    std::string path = std::string(base) + name;
    SDL_free(base);
    return path;
}

static std::string LoadPref(const char* name) {
    const std::string path = PrefPath(name);
    if (path.empty()) {
        return std::string();
    }
    std::ifstream in(path);
    if (!in) {
        return std::string();
    }
    std::string value;
    std::getline(in, value);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

static void SavePref(const char* name, const std::string& value) {
    const std::string path = PrefPath(name);
    if (path.empty()) {
        return;
    }
    std::ofstream out(path, std::ios::trunc);
    if (out) {
        out << value << "\n";
    }
}

static std::string LoadSavedIp() {
    const std::string ip = LoadPref("last_ip.txt");
    return IsValidIpv4(ip) ? ip : std::string();
}

static void SaveIp(const std::string& ip) {
    if (IsValidIpv4(ip)) {
        SavePref("last_ip.txt", ip);
    }
}

static RelaySet LoadSavedSet(bool& found) {
    const std::string value = LoadPref("relay_mode.txt");
    found = true;
    if (value == "mouse") {
        return RelaySet::Mouse;
    }
    if (value == "mic") {
        return RelaySet::Mic;
    }
    if (value == "both") {
        return RelaySet::Both;
    }
    found = false;
    return RelaySet::Mouse;
}

static void SaveSet(RelaySet set) {
    switch (set) {
    case RelaySet::Mouse:
        SavePref("relay_mode.txt", "mouse");
        break;
    case RelaySet::Mic:
        SavePref("relay_mode.txt", "mic");
        break;
    case RelaySet::Both:
        SavePref("relay_mode.txt", "both");
        break;
    }
}

static int ButtonIndex(uint8_t button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return 0;
    case SDL_BUTTON_RIGHT:
        return 1;
    case SDL_BUTTON_MIDDLE:
        return 2;
    case SDL_BUTTON_X1:
        return 3;
    case SDL_BUTTON_X2:
        return 4;
    default:
        return -1;
    }
}

static bool ParseModeStatus(const std::string& text, RelayMode& mode) {
    if (text.rfind("MODE:GAMEPLAY", 0) == 0) {
        mode = RelayMode::Gameplay;
        return true;
    }
    if (text.rfind("MODE:MENU", 0) == 0) {
        mode = RelayMode::Menu;
        return true;
    }
    return false;
}

static std::optional<std::pair<float, float>> ParseSyncStatus(const std::string& text) {
    size_t prefixLength = 0;
    if (text.rfind("SYNCW:", 0) == 0) {
        prefixLength = 6;
    } else if (text.rfind("SYNC:", 0) == 0) {
        prefixLength = 5;
    } else {
        return std::nullopt;
    }

    const size_t comma = text.find(',', prefixLength);
    if (comma == std::string::npos) {
        return std::nullopt;
    }

    char* end = nullptr;
    const float x = std::strtof(text.c_str() + prefixLength, &end);
    if (!end || static_cast<size_t>(end - text.c_str()) != comma) {
        return std::nullopt;
    }

    end = nullptr;
    const float y = std::strtof(text.c_str() + comma + 1, &end);
    if (!end || *end != '\0') {
        return std::nullopt;
    }

    return std::make_pair(x, y);
}

static bool IsStatusTerminator(char ch) {
    return ch == '\0' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static std::optional<std::pair<float, float>> ParsePairStatus(const std::string& text, const char* marker) {
    const size_t markerPos = text.find(marker);
    if (markerPos == std::string::npos) {
        return std::nullopt;
    }

    const size_t prefixLength = markerPos + std::strlen(marker);
    const size_t comma = text.find(',', prefixLength);
    if (comma == std::string::npos) {
        return std::nullopt;
    }

    char* end = nullptr;
    const float x = std::strtof(text.c_str() + prefixLength, &end);
    if (!end || static_cast<size_t>(end - text.c_str()) != comma) {
        return std::nullopt;
    }

    end = nullptr;
    const float y = std::strtof(text.c_str() + comma + 1, &end);
    if (!end || !IsStatusTerminator(*end)) {
        return std::nullopt;
    }

    return std::make_pair(x, y);
}

static std::optional<std::pair<float, float>> ParseDimensionStatus(const std::string& text, const char* markerText) {
    const size_t marker = text.find(markerText);
    if (marker == std::string::npos) {
        return std::nullopt;
    }

    const char* start = text.c_str() + marker + std::strlen(markerText);
    char* end = nullptr;
    const float width = std::strtof(start, &end);
    if (!end || *end != 'x') {
        return std::nullopt;
    }

    start = end + 1;
    end = nullptr;
    const float height = std::strtof(start, &end);
    if (width < 1.0f || height < 1.0f) {
        return std::nullopt;
    }
    if (!end || !IsStatusTerminator(*end)) {
        return std::nullopt;
    }

    return std::make_pair(width, height);
}

static std::optional<std::pair<float, float>> ParseSizeStatus(const std::string& text) {
    return ParseDimensionStatus(text, "size=");
}

static bool StatusHasMic(const std::string& text) {
    return text.find("mic=1") != std::string::npos;
}

#ifdef _WIN32
struct WinsockRuntime {
    WinsockRuntime() {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockRuntime() {
        if (ok) {
            WSACleanup();
        }
    }

    bool ok = false;
};

struct TimerResolution {
    // windows scheduler ticks at ~15.6ms by default, capping SDL_Delay and the loop
    // at ~64hz; request 1ms so the 240hz send pacing is actually reachable
    bool raised = (timeBeginPeriod(1) == TIMERR_NOERROR);
    ~TimerResolution() {
        if (raised) {
            timeEndPeriod(1);
        }
    }
};

static bool SocketWouldBlock() {
    const int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK || err == WSAEINTR || err == WSAETIMEDOUT;
}

static void CloseSocket(SocketHandle sock) {
    if (sock != kInvalidSocket) {
        closesocket(sock);
    }
}

static bool SetNonblocking(SocketHandle sock) {
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
}

static void SetReceiveTimeout(SocketHandle sock, int millis) {
    DWORD value = static_cast<DWORD>(millis);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof(value));
}
#else
struct WinsockRuntime {
    bool ok = true;
};

struct TimerResolution {};

static bool SocketWouldBlock() {
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR;
}

static void CloseSocket(SocketHandle sock) {
    if (sock != kInvalidSocket) {
        close(sock);
    }
}

static bool SetNonblocking(SocketHandle sock) {
    const int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
}

static void SetReceiveTimeout(SocketHandle sock, int millis) {
    timeval value{};
    value.tv_sec = millis / 1000;
    value.tv_usec = (millis % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof(value));
}
#endif

static bool MakeAddress(const std::string& host, uint16_t port, sockaddr_in& out) {
    out = {};
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    return inet_pton(AF_INET, host.c_str(), &out.sin_addr) == 1;
}

static std::string AddressText(const sockaddr_in& addr) {
    char text[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &addr.sin_addr, text, sizeof(text))) {
        return std::string();
    }
    return text;
}

static std::string LocalIpv4() {
    SocketHandle probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == kInvalidSocket) {
        return std::string();
    }
    sockaddr_in remote{};
    MakeAddress("8.8.8.8", 53, remote);
    std::string result;
    if (connect(probe, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == 0) {
        sockaddr_in local{};
        SockLen length = sizeof(local);
        if (getsockname(probe, reinterpret_cast<sockaddr*>(&local), &length) == 0) {
            result = AddressText(local);
        }
    }
    CloseSocket(probe);
    return IsValidIpv4(result) ? result : std::string();
}

static std::string SubnetPrefix(const std::string& ip) {
    const size_t dot = ip.rfind('.');
    return dot == std::string::npos ? std::string() : ip.substr(0, dot + 1);
}

static void WriteU32(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value & 0xFF);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    out[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

static uint32_t ReadU32(const uint8_t* in) {
    return static_cast<uint32_t>(in[0])
        | (static_cast<uint32_t>(in[1]) << 8)
        | (static_cast<uint32_t>(in[2]) << 16)
        | (static_cast<uint32_t>(in[3]) << 24);
}

static float FrameLevel(const int16_t* samples, int count) {
    if (count <= 0) {
        return 0.0f;
    }
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const double s = static_cast<double>(samples[i]) / 32768.0;
        sum += s * s;
    }
    const double rms = std::sqrt(sum / static_cast<double>(count));
    return Clamp(static_cast<float>(rms * 4.0), 0.0f, 1.0f);
}

class UdpTransport {
public:
    UdpTransport() = default;

    ~UdpTransport() {
        Close();
    }

    bool Open(const std::string& host, std::string& error) {
        Close();

        inputSock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (inputSock_ == kInvalidSocket) {
            error = "Could not create input UDP socket";
            return false;
        }

        statusSock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (statusSock_ == kInvalidSocket) {
            error = "Could not create status UDP socket";
            Close();
            return false;
        }

        int reuse = 1;
        setsockopt(statusSock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in statusAddr{};
        statusAddr.sin_family = AF_INET;
        statusAddr.sin_port = htons(kStatusPort);
        statusAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(statusSock_, reinterpret_cast<sockaddr*>(&statusAddr), sizeof(statusAddr)) != 0) {
            error = "Could not bind status UDP port " + std::to_string(kStatusPort);
            Close();
            return false;
        }

        if (!SetNonblocking(statusSock_) || !SetNonblocking(inputSock_)) {
            error = "Could not make relay sockets nonblocking";
            Close();
            return false;
        }

        if (!MakeAddress(host, kInputPort, destination_)) {
            error = "Invalid target IPv4 address";
            Close();
            return false;
        }

        host_ = host;
        return true;
    }

    void Close() {
        CloseSocket(inputSock_);
        CloseSocket(statusSock_);
        inputSock_ = kInvalidSocket;
        statusSock_ = kInvalidSocket;
    }

    bool IsOpen() const {
        return inputSock_ != kInvalidSocket && statusSock_ != kInvalidSocket;
    }

    bool Send(const std::string& packet) {
        if (inputSock_ == kInvalidSocket) {
            return false;
        }

        const int sent = sendto(
            inputSock_,
            packet.data(),
            static_cast<int>(packet.size()),
            0,
            reinterpret_cast<const sockaddr*>(&destination_),
            sizeof(destination_));
        const bool ok = sent == static_cast<int>(packet.size());
        g_packetLog.LogTx(packet, ok);
        return ok;
    }

    std::vector<std::string> ReceiveStatus() {
        std::vector<std::string> messages;
        if (statusSock_ == kInvalidSocket) {
            return messages;
        }

        Drain(statusSock_, messages, true);
        Drain(inputSock_, messages, false);
        return messages;
    }

private:
    void Drain(SocketHandle sock, std::vector<std::string>& messages, bool reportErrors) {
        if (sock == kInvalidSocket) {
            return;
        }
        for (;;) {
            char buffer[256]{};
            sockaddr_in from{};
            SockLen fromLen = sizeof(from);
            const int received = recvfrom(
                sock,
                buffer,
                static_cast<int>(sizeof(buffer) - 1),
                0,
                reinterpret_cast<sockaddr*>(&from),
                &fromLen);

            if (received < 0) {
                if (reportErrors && !SocketWouldBlock()) {
                    messages.emplace_back("STATUS_SOCKET_ERROR");
                }
                break;
            }

            if (from.sin_addr.s_addr == destination_.sin_addr.s_addr) {
                buffer[received] = '\0';
                messages.emplace_back(buffer);
                g_packetLog.LogRx(buffer);
            }
        }
    }

    SocketHandle inputSock_ = kInvalidSocket;
    SocketHandle statusSock_ = kInvalidSocket;
    sockaddr_in destination_{};
    std::string host_;
};

struct FinderResult {
    std::string ip;
    bool micCapable = false;
};

class XboxFinder {
public:
    ~XboxFinder() {
        Cancel();
    }

    void Start(const std::string& savedIp) {
        Cancel();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_.reset();
            progress_ = "Looking for your Xbox";
            round_ = 0;
        }
        cancel_ = false;
        running_ = true;
        thread_ = std::thread(&XboxFinder::Run, this, savedIp);
    }

    void Cancel() {
        cancel_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
        running_ = false;
    }

    bool Running() const {
        return running_.load();
    }

    std::optional<FinderResult> TakeResult() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::optional<FinderResult> out = result_;
        result_.reset();
        return out;
    }

    std::string Progress() {
        std::lock_guard<std::mutex> lock(mutex_);
        return progress_;
    }

    int Round() {
        std::lock_guard<std::mutex> lock(mutex_);
        return round_;
    }

private:
    void SetProgress(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_ = text;
    }

    void Finish(const FinderResult& result) {
        std::lock_guard<std::mutex> lock(mutex_);
        result_ = result;
        progress_ = "Found Xbox at " + result.ip;
    }

    void Run(std::string savedIp) {
        SocketHandle sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == kInvalidSocket) {
            SetProgress("Could not open a UDP socket");
            running_ = false;
            return;
        }
        int broadcast = 1;
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));
        SetNonblocking(sock);

        FinderResult found;
        if (!savedIp.empty()) {
            SetProgress("Trying " + savedIp);
            const double until = NowSeconds() + kSavedIpProbeSeconds;
            while (!cancel_ && NowSeconds() < until) {
                Ping(sock, savedIp);
                if (Listen(sock, 0.2, found)) {
                    CloseSocket(sock);
                    Finish(found);
                    running_ = false;
                    return;
                }
            }
        }

        const std::string localIp = LocalIpv4();
        const std::string prefix = SubnetPrefix(localIp);

        while (!cancel_) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++round_;
            }
            if (prefix.empty()) {
                SetProgress("Asking the network");
            } else {
                SetProgress("Scanning " + prefix + "1-254");
            }

            Ping(sock, "255.255.255.255");
            if (!prefix.empty()) {
                Ping(sock, prefix + "255");
                int burst = 0;
                for (int host = 1; host <= 254 && !cancel_; ++host) {
                    const std::string candidate = prefix + std::to_string(host);
                    if (candidate == localIp) {
                        continue;
                    }
                    Ping(sock, candidate);
                    if (++burst >= 32) {
                        burst = 0;
                        if (Listen(sock, 0.015, found)) {
                            CloseSocket(sock);
                            Finish(found);
                            running_ = false;
                            return;
                        }
                    }
                }
            }

            if (Listen(sock, kSweepListenSeconds, found)) {
                CloseSocket(sock);
                Finish(found);
                running_ = false;
                return;
            }

            SetProgress("Xbox not found yet, is the launcher open?");
            const double pauseUntil = NowSeconds() + kSweepPauseSeconds;
            while (!cancel_ && NowSeconds() < pauseUntil) {
                if (Listen(sock, 0.1, found)) {
                    CloseSocket(sock);
                    Finish(found);
                    running_ = false;
                    return;
                }
            }
        }

        CloseSocket(sock);
        running_ = false;
    }

    static void Ping(SocketHandle sock, const std::string& ip) {
        sockaddr_in to{};
        if (!MakeAddress(ip, kInputPort, to)) {
            return;
        }
        static const char ping[] = "ping";
        sendto(sock, ping, static_cast<int>(sizeof(ping) - 1), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    }

    bool Listen(SocketHandle sock, double seconds, FinderResult& out) {
        const double until = NowSeconds() + seconds;
        for (;;) {
            char buffer[256]{};
            sockaddr_in from{};
            SockLen fromLen = sizeof(from);
            const int received = recvfrom(sock, buffer, static_cast<int>(sizeof(buffer) - 1), 0,
                reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (received > 0) {
                buffer[received] = '\0';
                const std::string text(buffer);
                if (text.rfind(kReadyPrefix, 0) == 0) {
                    out.ip = AddressText(from);
                    out.micCapable = StatusHasMic(text);
                    if (IsValidIpv4(out.ip)) {
                        return true;
                    }
                }
                continue;
            }
            if (cancel_ || NowSeconds() >= until) {
                return false;
            }
            SDL_Delay(5);
        }
    }

    std::thread thread_;
    std::atomic<bool> cancel_{ false };
    std::atomic<bool> running_{ false };
    std::mutex mutex_;
    std::optional<FinderResult> result_;
    std::string progress_;
    int round_ = 0;
};

class MicSender {
public:
    ~MicSender() {
        Stop();
    }

    static std::vector<std::string> DeviceNames() {
        std::vector<std::string> names;
        names.emplace_back(kDefaultMicName);
        int count = 0;
        SDL_AudioDeviceID* ids = SDL_GetAudioRecordingDevices(&count);
        if (ids) {
            for (int i = 0; i < count; ++i) {
                const char* name = SDL_GetAudioDeviceName(ids[i]);
                if (name && *name) {
                    names.emplace_back(name);
                }
            }
            SDL_free(ids);
        }
        return names;
    }

    bool Start(const std::string& host, const std::string& deviceName, std::string& error) {
        Stop();
        fellBack_ = false;

        SDL_AudioDeviceID device = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
        if (!deviceName.empty() && deviceName != kDefaultMicName) {
            bool matched = false;
            int count = 0;
            SDL_AudioDeviceID* ids = SDL_GetAudioRecordingDevices(&count);
            if (ids) {
                for (int i = 0; i < count; ++i) {
                    const char* name = SDL_GetAudioDeviceName(ids[i]);
                    if (name && deviceName == name) {
                        device = ids[i];
                        matched = true;
                        break;
                    }
                }
                SDL_free(ids);
            }
            fellBack_ = !matched;
        }

        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16LE;
        spec.channels = 1;
        spec.freq = kMicSampleRate;
        stream_ = SDL_OpenAudioDeviceStream(device, &spec, nullptr, nullptr);
        if (!stream_) {
            error = std::string("Mic open failed: ") + SDL_GetError();
            return false;
        }

        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ == kInvalidSocket) {
            error = "Could not create mic UDP socket";
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
            return false;
        }

        SetHost(host);
        activeName_ = fellBack_ || deviceName.empty() ? std::string(kDefaultMicName) : deviceName;
        packets_ = 0;
        level_ = 0.0f;
        SDL_ResumeAudioStreamDevice(stream_);
        running_ = true;
        thread_ = std::thread(&MicSender::Run, this);
        return true;
    }

    void Stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
        if (stream_) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
        CloseSocket(sock_);
        sock_ = kInvalidSocket;
        level_ = 0.0f;
    }

    bool Running() const {
        return running_.load();
    }

    bool FellBack() const {
        return fellBack_;
    }

    const std::string& ActiveName() const {
        return activeName_;
    }

    void SetHost(const std::string& host) {
        std::lock_guard<std::mutex> lock(hostMutex_);
        MakeAddress(host, kMicPort, destination_);
    }

    float Level() const {
        return level_.load(std::memory_order_relaxed);
    }

    uint64_t Packets() const {
        return packets_.load(std::memory_order_relaxed);
    }

private:
    void Run() {
        std::array<uint8_t, kMicHeaderBytes + kMicFrameBytes> packet{};
        std::memcpy(packet.data(), "BMA1", 4);
        WriteU32(packet.data() + 8, static_cast<uint32_t>(kMicSampleRate));
        packet[12] = 1;
        packet[13] = 16;
        packet[14] = static_cast<uint8_t>(kMicFrameSamples & 0xFF);
        packet[15] = static_cast<uint8_t>((kMicFrameSamples >> 8) & 0xFF);
        uint32_t seq = 0;

        while (running_.load(std::memory_order_relaxed)) {
            int available = SDL_GetAudioStreamAvailable(stream_);
            if (available > kMicFrameBytes * 20) {
                SDL_ClearAudioStream(stream_);
                available = 0;
            }
            while (available >= kMicFrameBytes && running_.load(std::memory_order_relaxed)) {
                const int got = SDL_GetAudioStreamData(stream_, packet.data() + kMicHeaderBytes, kMicFrameBytes);
                if (got != kMicFrameBytes) {
                    break;
                }
                WriteU32(packet.data() + 4, seq++);
                sockaddr_in to{};
                {
                    std::lock_guard<std::mutex> lock(hostMutex_);
                    to = destination_;
                }
                sendto(sock_, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
                    reinterpret_cast<const sockaddr*>(&to), sizeof(to));
                packets_.fetch_add(1, std::memory_order_relaxed);
                level_.store(FrameLevel(reinterpret_cast<const int16_t*>(packet.data() + kMicHeaderBytes), kMicFrameSamples),
                    std::memory_order_relaxed);
                available -= kMicFrameBytes;
            }
            SDL_Delay(4);
        }
    }

    SDL_AudioStream* stream_ = nullptr;
    SocketHandle sock_ = kInvalidSocket;
    sockaddr_in destination_{};
    std::mutex hostMutex_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    std::atomic<float> level_{ 0.0f };
    std::atomic<uint64_t> packets_{ 0 };
    std::string activeName_;
    bool fellBack_ = false;
};

class GameAudioPlayer {
public:
    ~GameAudioPlayer() {
        Stop();
    }

    bool Start(const std::string& host, std::string& error) {
        Stop();
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ == kInvalidSocket) {
            error = "Could not create game audio socket";
            return false;
        }
        int size = 1 << 20;
        setsockopt(sock_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&size), sizeof(size));
        SetReceiveTimeout(sock_, 100);
        SetHost(host);
        packets_ = 0;
        level_ = 0.0f;
        playing_ = false;
        running_ = true;
        thread_ = std::thread(&GameAudioPlayer::Run, this);
        return true;
    }

    void Stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
        if (stream_) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
        CloseSocket(sock_);
        sock_ = kInvalidSocket;
        playing_ = false;
        level_ = 0.0f;
    }

    bool Running() const {
        return running_.load();
    }

    bool Playing() const {
        return playing_.load();
    }

    void SetHost(const std::string& host) {
        std::lock_guard<std::mutex> lock(hostMutex_);
        MakeAddress(host, kGameAudioPort, destination_);
    }

    float Level() const {
        return level_.load(std::memory_order_relaxed);
    }

    uint64_t Packets() const {
        return packets_.load(std::memory_order_relaxed);
    }

private:
    void Run() {
        std::array<uint8_t, 2048> buffer{};
        double nextKeepalive = 0.0;
        int rate = 0;
        int channels = 0;
        double lastPacket = 0.0;

        while (running_.load(std::memory_order_relaxed)) {
            const double now = NowSeconds();
            sockaddr_in to{};
            {
                std::lock_guard<std::mutex> lock(hostMutex_);
                to = destination_;
            }
            if (now >= nextKeepalive) {
                static const char keepalive[] = "BMAS";
                sendto(sock_, keepalive, 4, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
                nextKeepalive = now + kGameAudioKeepaliveSeconds;
            }

            sockaddr_in from{};
            SockLen fromLen = sizeof(from);
            const int received = recvfrom(sock_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (received <= 0) {
                if (playing_ && now - lastPacket > 3.0) {
                    playing_ = false;
                    level_ = 0.0f;
                }
                continue;
            }
            if (from.sin_addr.s_addr != to.sin_addr.s_addr || received < 16) {
                continue;
            }
            if (std::memcmp(buffer.data(), "BMA2", 4) != 0) {
                continue;
            }
            const int packetRate = static_cast<int>(ReadU32(buffer.data() + 8));
            const int packetChannels = buffer[12];
            const int bits = buffer[13];
            const int frames = buffer[14] | (buffer[15] << 8);
            if (bits != 16 || (packetChannels != 1 && packetChannels != 2) || frames == 0) {
                continue;
            }
            if (packetRate < 8000 || packetRate > 384000) {
                continue;
            }
            const int payload = frames * packetChannels * 2;
            if (received != 16 + payload) {
                continue;
            }

            if (!stream_ || rate != packetRate || channels != packetChannels) {
                if (stream_) {
                    SDL_DestroyAudioStream(stream_);
                    stream_ = nullptr;
                }
                SDL_AudioSpec spec{};
                spec.format = SDL_AUDIO_S16LE;
                spec.channels = packetChannels;
                spec.freq = packetRate;
                stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
                if (!stream_) {
                    SDL_Delay(500);
                    continue;
                }
                SDL_ResumeAudioStreamDevice(stream_);
                rate = packetRate;
                channels = packetChannels;
            }

            const int capBytes = static_cast<int>(rate * channels * 2 * kGameAudioQueueCapSeconds);
            if (SDL_GetAudioStreamQueued(stream_) > capBytes) {
                SDL_ClearAudioStream(stream_);
            }
            SDL_PutAudioStreamData(stream_, buffer.data() + 16, payload);
            packets_.fetch_add(1, std::memory_order_relaxed);
            level_.store(FrameLevel(reinterpret_cast<const int16_t*>(buffer.data() + 16), frames * channels), std::memory_order_relaxed);
            lastPacket = NowSeconds();
            playing_ = true;
        }
    }

    SDL_AudioStream* stream_ = nullptr;
    SocketHandle sock_ = kInvalidSocket;
    sockaddr_in destination_{};
    std::mutex hostMutex_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> playing_{ false };
    std::atomic<float> level_{ 0.0f };
    std::atomic<uint64_t> packets_{ 0 };
};

class RelayApp {
public:
    RelayApp(SDL_Window* window, SDL_Renderer* renderer)
        : window_(window), renderer_(renderer) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        RefreshWindowSize();
        bool savedSet = false;
        set_ = LoadSavedSet(savedSet);
        setChosen_ = savedSet;
        micDevice_ = LoadPref("mic_device.txt");
        micDeviceChosen_ = !micDevice_.empty();
        if (micDevice_.empty()) {
            micDevice_ = kDefaultMicName;
        }
        gameAudioWanted_ = LoadPref("game_audio.txt") == "1";
        StartDiscovery();
        netRunning_ = true;
        netThread_ = std::thread(&RelayApp::NetLoop, this);
    }

    ~RelayApp() {
        netRunning_ = false;
        if (netThread_.joinable()) {
            netThread_.join();
        }
        finder_.Cancel();
        mic_.Stop();
        gameAudio_.Stop();
        ReleaseAllButtons();
        SDL_StopTextInput(window_);
        SetMouseCapture(false);
    }

    bool Running() const {
        return running_;
    }

    void HandleEvent(const SDL_Event& event) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            running_ = false;
            return;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            RefreshWindowSize();
            return;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (WantMouseCapture()) {
                SetMouseCapture(true);
            }
            return;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            SetMouseCapture(false);
            CancelHold();
            ReleaseAllButtons();
            return;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            CancelHold();
            ReleaseAllButtons();
            return;
        case SDL_EVENT_TEXT_INPUT:
            if (enteringIp_) {
                AppendIpText(event.text.text ? event.text.text : "");
            }
            return;
        case SDL_EVENT_KEY_DOWN:
            HandleKey(event.key.key);
            return;
        case SDL_EVENT_MOUSE_MOTION:
            if (RelayingMouse()) {
                AddMotion(event.motion.xrel, event.motion.yrel);
            }
            if (holdAction_ != UiAction::None && !PointerOverHoldButton(event.motion.x, event.motion.y)) {
                CancelHold();
            }
            return;
        case SDL_EVENT_MOUSE_WHEEL:
            if (RelayingMouse()) {
                const float scroll = event.wheel.integer_y != 0 ? static_cast<float>(event.wheel.integer_y) : event.wheel.y;
                pendingScroll_ += scroll;
            }
            return;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            HandleMouseButton(event);
            return;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
            HandleFingerButton(event);
            return;
        case SDL_EVENT_FINGER_MOTION:
            if (RelayingMouse() && !IsTouchControlFinger(event.tfinger.fingerID)) {
                AddMotion(event.tfinger.dx * static_cast<float>(windowWidth_), event.tfinger.dy * static_cast<float>(windowHeight_));
            }
            return;
        default:
            return;
        }
    }

    void Tick() {
        PollFinder();
        PollStatus();
        SendConnectionProbe();
        CheckLink();
        PollMicPermission();
        RepeatHeldTouchScroll();
        UpdateHold();
        g_packetLog.MaybeFlush();
    }

    void Render() {
        const float scale = UiScale();
        const float uiWidth = UiAreaWidth();
        const float uiHeight = UiAreaHeight();

        // otherwise the 8px font draws 1x and ios upscales it
        const float renderScale = scale * pixelDensity_;

        SDL_SetRenderViewport(renderer_, nullptr);
        SDL_SetRenderScale(renderer_, renderScale, renderScale);
        SDL_SetRenderDrawColor(renderer_, 9, 12, 18, 255);
        SDL_RenderClear(renderer_);

        if (safeArea_.x != 0 || safeArea_.y != 0) {
            const SDL_Rect viewport{
                static_cast<int>(std::lround(safeArea_.x / scale)),
                static_cast<int>(std::lround(safeArea_.y / scale)),
                static_cast<int>(std::lround(safeArea_.w / scale)),
                static_cast<int>(std::lround(safeArea_.h / scale))
            };
            SDL_SetRenderViewport(renderer_, &viewport);
        }

        UpdateLevels();

        if (discovering_) {
            RenderDiscoverScreen(uiWidth, uiHeight);
        } else if (pickingMic_) {
            RenderMicPickScreen(uiWidth, uiHeight);
        } else if (pickingSet_) {
            RenderPickScreen(uiWidth, uiHeight);
        } else if (enteringIp_) {
            RenderIpScreen(uiWidth, uiHeight);
        } else if (set_ == RelaySet::Mic) {
            RenderMicScreen(uiWidth, uiHeight);
        } else {
            RenderRelayScreen(uiWidth, uiHeight);
        }

        SDL_SetRenderViewport(renderer_, nullptr);
        SDL_RenderPresent(renderer_);
    }

    void PumpAndTick() {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            std::lock_guard<std::mutex> lock(sendMutex_);
            HandleEvent(event);
        }
        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            Tick();
        }
    }

private:
    void NetLoop() {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
        double nextTick = NowSeconds();
        while (netRunning_.load(std::memory_order_relaxed)) {
            {
                std::lock_guard<std::mutex> lock(sendMutex_);
                SendPendingPackets();
            }
            nextTick += kSendIntervalSeconds;
            const double now = NowSeconds();
            if (nextTick < now) {
                nextTick = now;
            }
            SleepUntil(nextTick);
        }
    }

    bool MouseEnabled() const {
        return set_ != RelaySet::Mic;
    }

    bool MicEnabled() const {
        return set_ != RelaySet::Mouse;
    }

    bool OnRelayScreen() const {
        return hostSet_ && !discovering_ && !pickingSet_ && !pickingMic_ && !enteringIp_;
    }

    bool RelayingMouse() const {
        return OnRelayScreen() && !menuOpen_ && MouseEnabled();
    }

    bool WantMouseCapture() const {
        return RelayingMouse();
    }

    float UiScale() const {
        // under 1 only works because the retina render scale keeps small text sharp
        const float minScale = kTouchLayout ? 0.75f : 1.0f;
        return Clamp(static_cast<float>(safeArea_.h) / 720.0f, minScale, 3.0f);
    }

    float UiAreaWidth() const {
        return static_cast<float>(safeArea_.w) / UiScale();
    }

    float UiAreaHeight() const {
        return static_cast<float>(safeArea_.h) / UiScale();
    }

    std::vector<Button> BuildButtons(float uiWidth, float uiHeight) const {
        std::vector<Button> buttons;

        if (discovering_) {
            const float width = kTouchLayout ? 176.0f : 156.0f;
            const float height = kTouchLayout ? 46.0f : 34.0f;
            const float gap = kTouchLayout ? 18.0f : 16.0f;
            const float total = (width * 2.0f) + gap;
            const float x = (uiWidth - total) * 0.5f;
            const float y = DiscoverTop(uiHeight) + 150.0f;
            buttons.push_back({ SDL_FRect{ x, y, width, height }, "Enter IP", UiAction::EnterIp });
            buttons.push_back({ SDL_FRect{ x + width + gap, y, width, height }, "Quit", UiAction::Quit });
            return buttons;
        }

        if (pickingMic_) {
            const float height = kTouchLayout ? 44.0f : 34.0f;
            const float gap = kTouchLayout ? 10.0f : 8.0f;
            const float width = std::min(560.0f, uiWidth - 64.0f);
            const float x = (uiWidth - width) * 0.5f;
            float y = MicPickTop(uiHeight) + 52.0f;
            const size_t shown = std::min<size_t>(micList_.size(), 8);
            for (size_t i = 0; i < shown; ++i) {
                Button entry{ SDL_FRect{ x, y, width, height }, FitText(micList_[i], width - 24.0f), UiAction::PickMicDevice };
                entry.listIndex = static_cast<int>(i);
                buttons.push_back(entry);
                y += height + gap;
            }
            const float backWidth = kTouchLayout ? 176.0f : 156.0f;
            buttons.push_back({ SDL_FRect{ (uiWidth - backWidth) * 0.5f, y + 12.0f, backWidth, height }, "Back", UiAction::Resume });
            return buttons;
        }

        if (pickingSet_) {
            const float width = kTouchLayout ? 176.0f : 168.0f;
            const float height = kTouchLayout ? 64.0f : 52.0f;
            const float gap = kTouchLayout ? 18.0f : 16.0f;
            const float total = (width * 3.0f) + (gap * 2.0f);
            float x = (uiWidth - total) * 0.5f;
            const float y = PickTop(uiHeight) + 82.0f;
            buttons.push_back({ SDL_FRect{ x, y, width, height }, "Mouse", UiAction::PickMouse });
            x += width + gap;
            Button both{ SDL_FRect{ x, y, width, height }, "Mouse + Mic", micCapable_ ? UiAction::PickBoth : UiAction::None };
            both.disabled = !micCapable_;
            buttons.push_back(both);
            x += width + gap;
            Button mic{ SDL_FRect{ x, y, width, height }, "Mic", micCapable_ ? UiAction::PickMic : UiAction::None };
            mic.disabled = !micCapable_;
            buttons.push_back(mic);
            if (hostSet_) {
                const float backWidth = kTouchLayout ? 176.0f : 156.0f;
                const float backHeight = kTouchLayout ? 46.0f : 34.0f;
                buttons.push_back({ SDL_FRect{ (uiWidth - backWidth) * 0.5f, y + height + 64.0f, backWidth, backHeight }, "Back", UiAction::Resume });
            }
            return buttons;
        }

        if (enteringIp_) {
            const float width = kTouchLayout ? 176.0f : 156.0f;
            const float height = kTouchLayout ? 46.0f : 34.0f;
            const float gap = kTouchLayout ? 18.0f : 16.0f;
            const float total = (width * 2.0f) + gap;
            const float x = (uiWidth - total) * 0.5f;
            const float top = IpScreenTop(uiHeight);

            if (kTouchLayout) {
                const float keyWidth = 78.0f;
                const float keyHeight = 44.0f;
                const float keyGap = 8.0f;
                const float keyTotal = (keyWidth * 3.0f) + (keyGap * 2.0f);
                const float keyX = (uiWidth - keyTotal) * 0.5f;
                const float keyY = top + 150.0f;
                const std::array<const char*, 12> labels{
                    "1", "2", "3",
                    "4", "5", "6",
                    "7", "8", "9",
                    ".", "0", "Del"
                };

                for (size_t i = 0; i < labels.size(); ++i) {
                    const float col = static_cast<float>(i % 3);
                    const float row = static_cast<float>(i / 3);
                    const char* label = labels[i];
                    Button key{};
                    key.rect = SDL_FRect{
                        keyX + (col * (keyWidth + keyGap)),
                        keyY + (row * (keyHeight + keyGap)),
                        keyWidth,
                        keyHeight
                    };
                    key.label = label;
                    if (std::strcmp(label, "Del") == 0) {
                        key.backspace = true;
                    } else {
                        key.inputText = label;
                    }
                    buttons.push_back(key);
                }
            }

            const float y = top + (kTouchLayout ? 374.0f : 168.0f);
            buttons.push_back({
                SDL_FRect{ x, y, width, height },
                connecting_ ? "Checking" : "Connect",
                connecting_ ? UiAction::None : UiAction::Connect
            });
            buttons.push_back({ SDL_FRect{ x + width + gap, y, width, height }, hostSet_ || connecting_ ? "Cancel" : "Scan", UiAction::CancelIp });
            return buttons;
        }

        if (menuOpen_) {
            return BuildMenuButtons(uiWidth, uiHeight);
        }

        if (set_ == RelaySet::Mic) {
            const float radius = MicRingRadius(uiHeight);
            const float centerY = MicCenterY(uiHeight);
            Button ring{ SDL_FRect{ (uiWidth * 0.5f) - radius - 24.0f, centerY - radius - 24.0f, (radius + 24.0f) * 2.0f, (radius + 24.0f) * 2.0f }, "", UiAction::ToggleMic };
            buttons.push_back(ring);

            const float width = kTouchLayout ? 132.0f : 120.0f;
            const float height = kTouchLayout ? 44.0f : 34.0f;
            const float gap = 12.0f;
            const float total = (width * 3.0f) + (gap * 2.0f);
            float x = (uiWidth - total) * 0.5f;
            const float y = uiHeight - height - 16.0f;
            buttons.push_back({ SDL_FRect{ x, y, width, height }, "Menu", UiAction::OpenMenu });
            x += width + gap;
            buttons.push_back({ SDL_FRect{ x, y, width, height }, "Mic input", UiAction::OpenMicPicker });
            x += width + gap;
            buttons.push_back({ SDL_FRect{ x, y, width, height }, gameAudioWanted_ ? "Game audio on" : "Game audio off", UiAction::ToggleGameAudio });
            return buttons;
        }

        if (kTouchLayout) {
            const float margin = 16.0f;
            const float utilityWidth = 118.0f;
            const float utilityHeight = 44.0f;
            const float utilityGap = 10.0f;
            const float utilityTotal = (utilityWidth * 4.0f) + (utilityGap * 3.0f);
            float utilityX = std::max(margin, (uiWidth - utilityTotal) * 0.5f);
            const float utilityY = uiHeight - utilityHeight - margin;

            buttons.push_back({ SDL_FRect{ utilityX, utilityY, utilityWidth, utilityHeight }, "Menu", UiAction::OpenMenu });
            utilityX += utilityWidth + utilityGap;
            buttons.push_back({ SDL_FRect{ utilityX, utilityY, utilityWidth, utilityHeight }, "Toggle", UiAction::ToggleMode });
            utilityX += utilityWidth + utilityGap;
            buttons.push_back({ SDL_FRect{ utilityX, utilityY, utilityWidth, utilityHeight }, "Release", UiAction::ReleaseButtons });
            utilityX += utilityWidth + utilityGap;
            buttons.push_back({ SDL_FRect{ utilityX, utilityY, utilityWidth, utilityHeight }, "Quit", UiAction::Quit });

            if (MicEnabled()) {
                buttons.push_back({ MicPadRect(uiWidth), micOn_ ? "Mic on" : "Mic off", UiAction::ToggleMic });
            }

            const float padWidth = 132.0f;
            const float padHeight = 48.0f;
            const float padGap = 10.0f;
            const float padTop = 172.0f;
            const float columnHeight = (padHeight * 4.0f) + (padGap * 3.0f);

            // column needs 222ui, iphone landscape leaves about 130
            const bool padGrid = (utilityY - padGap - padTop) < columnHeight;

            if (padGrid) {
                const float gridWidth = (padWidth * 2.0f) + padGap;
                const float gridX = uiWidth - gridWidth - margin;
                const float gridY = std::max(padTop, utilityY - padGap - (padHeight * 2.0f) - padGap);
                const float colB = gridX + padWidth + padGap;
                const float rowB = gridY + padHeight + padGap;

                buttons.push_back({ SDL_FRect{ gridX, gridY, padWidth, padHeight }, "Hold L", UiAction::None, 0, 0.0f });
                buttons.push_back({ SDL_FRect{ colB, gridY, padWidth, padHeight }, "Hold R", UiAction::None, 1, 0.0f });
                buttons.push_back({ SDL_FRect{ gridX, rowB, padWidth, padHeight }, "Wheel Up", UiAction::None, -1, kTouchScrollStep });
                buttons.push_back({ SDL_FRect{ colB, rowB, padWidth, padHeight }, "Wheel Down", UiAction::None, -1, -kTouchScrollStep });
                return buttons;
            }

            const float padX = uiWidth - padWidth - margin;
            float padY = std::max(padTop, utilityY - ((padHeight + padGap) * 4.0f) - 10.0f);

            buttons.push_back({ SDL_FRect{ padX, padY, padWidth, padHeight }, "Hold L", UiAction::None, 0, 0.0f });
            padY += padHeight + padGap;
            buttons.push_back({ SDL_FRect{ padX, padY, padWidth, padHeight }, "Hold R", UiAction::None, 1, 0.0f });
            padY += padHeight + padGap;
            buttons.push_back({ SDL_FRect{ padX, padY, padWidth, padHeight }, "Wheel Up", UiAction::None, -1, kTouchScrollStep });
            padY += padHeight + padGap;
            buttons.push_back({ SDL_FRect{ padX, padY, padWidth, padHeight }, "Wheel Down", UiAction::None, -1, -kTouchScrollStep });
            return buttons;
        }

        return buttons;
    }

    SDL_FRect MicPadRect(float uiWidth) const {
        const float width = 132.0f;
        const float height = 48.0f;
        return SDL_FRect{ uiWidth - width - 16.0f, 16.0f, width, height };
    }

    std::vector<std::string> MenuLabels(std::vector<UiAction>& actions) const {
        std::vector<std::string> labels;
        labels.push_back("Resume");
        actions.push_back(UiAction::Resume);
        labels.push_back("Mode: " + SetName(set_));
        actions.push_back(UiAction::ChangeSet);
        labels.push_back("Change IP");
        actions.push_back(UiAction::ChangeIp);
        if (MouseEnabled()) {
            labels.push_back("Toggle");
            actions.push_back(UiAction::ToggleMode);
            labels.push_back("Release");
            actions.push_back(UiAction::ReleaseButtons);
        }
        if (MicEnabled()) {
            labels.push_back(micOn_ ? "Mic: on" : "Mic: off");
            actions.push_back(UiAction::ToggleMic);
            labels.push_back("Mic input");
            actions.push_back(UiAction::OpenMicPicker);
        }
        labels.push_back(gameAudioWanted_ ? "Game audio: on" : "Game audio: off");
        actions.push_back(UiAction::ToggleGameAudio);
        labels.push_back("Quit");
        actions.push_back(UiAction::Quit);
        return labels;
    }

    float MenuPanelHeight() const {
        std::vector<UiAction> actions;
        const size_t count = MenuLabels(actions).size();
        const float buttonHeight = kTouchLayout ? 44.0f : 34.0f;
        const float gap = kTouchLayout ? 12.0f : 10.0f;
        const size_t rows = (count + 1) / 2;
        return 74.0f + (static_cast<float>(rows) * (buttonHeight + gap)) + 10.0f;
    }

    std::vector<Button> BuildMenuButtons(float uiWidth, float uiHeight) const {
        std::vector<Button> buttons;
        const float buttonWidth = 200.0f;
        const float buttonHeight = kTouchLayout ? 44.0f : 34.0f;
        const float gap = kTouchLayout ? 12.0f : 10.0f;
        const float panelHeight = MenuPanelHeight();
        const float left = (uiWidth - ((buttonWidth * 2.0f) + gap)) * 0.5f;
        const float top = ((uiHeight - panelHeight) * 0.5f) + 74.0f;

        std::vector<UiAction> actions;
        const std::vector<std::string> labels = MenuLabels(actions);
        const size_t rows = (labels.size() + 1) / 2;
        for (size_t i = 0; i < labels.size(); ++i) {
            const float column = static_cast<float>(i / rows);
            const float row = static_cast<float>(i % rows);
            buttons.push_back({
                SDL_FRect{ left + (column * (buttonWidth + gap)), top + (row * (buttonHeight + gap)), buttonWidth, buttonHeight },
                labels[i],
                actions[i]
            });
        }
        return buttons;
    }

    std::optional<Button> HitButtonAtUi(float uiX, float uiY) const {
        for (const Button& button : BuildButtons(UiAreaWidth(), UiAreaHeight())) {
            if (uiX >= button.rect.x && uiX <= button.rect.x + button.rect.w &&
                uiY >= button.rect.y && uiY <= button.rect.y + button.rect.h) {
                return button;
            }
        }

        return std::nullopt;
    }

    UiAction HitActionAtUi(float uiX, float uiY) const {
        const std::optional<Button> button = HitButtonAtUi(uiX, uiY);
        return button ? button->action : UiAction::None;
    }

    std::optional<Button> HitButton(float windowX, float windowY) const {
        const float scale = UiScale();
        return HitButtonAtUi(
            (windowX - static_cast<float>(safeArea_.x)) / scale,
            (windowY - static_cast<float>(safeArea_.y)) / scale);
    }

    UiAction HitAction(float windowX, float windowY) const {
        const std::optional<Button> button = HitButton(windowX, windowY);
        return button ? button->action : UiAction::None;
    }

    void ExecuteAction(UiAction action) {
        switch (action) {
        case UiAction::Connect:
            TryConnect();
            break;
        case UiAction::CancelIp:
            if (connecting_) {
                connecting_ = false;
                transport_.Close();
                ipError_.clear();
                if (autoConnect_) {
                    StartDiscovery();
                } else {
                    BeginIpTextInput();
                }
                break;
            }
            if (hostSet_) {
                enteringIp_ = false;
                ipError_.clear();
                SDL_StopTextInput(window_);
                if (WantMouseCapture()) {
                    SetMouseCapture(true);
                }
            } else {
                StartDiscovery();
            }
            break;
        case UiAction::ChangeIp:
        case UiAction::EnterIp:
            ReleaseAllButtons();
            finder_.Cancel();
            discovering_ = false;
            reconnecting_ = false;
            menuOpen_ = false;
            enteringIp_ = true;
            ipBuffer_ = host_.empty() ? LoadSavedIp() : host_;
            ipError_.clear();
            SetMouseCapture(false);
            BeginIpTextInput();
            break;
        case UiAction::ToggleMode:
            mode_ = mode_ == RelayMode::Gameplay ? RelayMode::Menu : RelayMode::Gameplay;
            if (mode_ == RelayMode::Menu) {
                SyncMenuCenter();
            }
            break;
        case UiAction::ReleaseButtons:
            ReleaseAllButtons();
            break;
        case UiAction::Resume:
            ResumeRelay();
            break;
        case UiAction::Quit:
            running_ = false;
            break;
        case UiAction::PickMouse:
            ChooseSet(RelaySet::Mouse);
            break;
        case UiAction::PickMic:
            ChooseSet(RelaySet::Mic);
            break;
        case UiAction::PickBoth:
            ChooseSet(RelaySet::Both);
            break;
        case UiAction::ChangeSet:
            ReleaseAllButtons();
            menuOpen_ = false;
            pickingSet_ = true;
            SetMouseCapture(false);
            break;
        case UiAction::ToggleMic:
            if (micOn_) {
                StopMic();
            } else {
                StartMic();
            }
            break;
        case UiAction::ToggleGameAudio:
            gameAudioWanted_ = !gameAudioWanted_;
            SavePref("game_audio.txt", gameAudioWanted_ ? "1" : "0");
            ApplyGameAudio();
            break;
        case UiAction::OpenMicPicker:
            OpenMicPicker(menuOpen_ ? MicPickReturn::Menu : MicPickReturn::Relay);
            break;
        case UiAction::PickMicDevice:
            break;
        case UiAction::OpenMenu:
            OpenRelayMenu();
            break;
        case UiAction::None:
            break;
        }
    }

    bool ExecuteButtonRelease(const Button& button) {
        if (button.backspace) {
            if (!ipBuffer_.empty()) {
                ipBuffer_.pop_back();
            }
            return true;
        }

        if (!button.inputText.empty()) {
            AppendIpText(button.inputText.c_str());
            return true;
        }

        if (button.action != UiAction::None) {
            ExecuteAction(button.action);
            return true;
        }

        return false;
    }

    bool ActionNeedsHold(UiAction action) const {
        if (enteringIp_ || discovering_ || pickingSet_ || pickingMic_) {
            return false;
        }
        if (action == UiAction::ToggleMic) {
            return kTouchLayout && MouseEnabled() && !menuOpen_;
        }
        if (set_ == RelaySet::Mic && !menuOpen_) {
            return action == UiAction::Quit;
        }
        return action != UiAction::None
            && action != UiAction::ToggleMode
            && action != UiAction::Connect
            && action != UiAction::Resume
            && action != UiAction::OpenMenu
            && action != UiAction::ChangeSet
            && action != UiAction::ToggleGameAudio
            && action != UiAction::OpenMicPicker
            && action != UiAction::PickMicDevice;
    }

    double HoldSeconds(UiAction action) const {
        return action == UiAction::ToggleMic ? kMicHoldSeconds : kButtonHoldSeconds;
    }

    void BeginHold(UiAction action) {
        holdAction_ = action;
        holdStart_ = NowSeconds();
    }

    void CancelHold() {
        holdAction_ = UiAction::None;
    }

    void UpdateHold() {
        if (holdAction_ == UiAction::None) {
            return;
        }
        if (NowSeconds() - holdStart_ >= HoldSeconds(holdAction_)) {
            const UiAction action = holdAction_;
            holdAction_ = UiAction::None;
            ExecuteAction(action);
        }
    }

    float HoldProgress(const Button& button) const {
        if (holdAction_ == UiAction::None || button.action != holdAction_) {
            return 0.0f;
        }
        const float elapsed = static_cast<float>(NowSeconds() - holdStart_);
        return Clamp(elapsed / static_cast<float>(HoldSeconds(holdAction_)), 0.0f, 1.0f);
    }

    bool PointerOverHoldButton(float windowX, float windowY) const {
        if (holdAction_ == UiAction::None) {
            return false;
        }
        const std::optional<Button> hit = HitButton(windowX, windowY);
        if (hit && hit->action == holdAction_) {
            return true;
        }
        const std::optional<Button> relayButton = HitButtonAtRelayCursor();
        if (relayButton && relayButton->action == holdAction_) {
            return true;
        }
        return false;
    }

    void OnButtonPressed(const Button& button) {
        if (button.backspace || !button.inputText.empty()) {
            ExecuteButtonRelease(button);
            return;
        }
        if (button.action == UiAction::None || button.disabled) {
            return;
        }
        if (button.action == UiAction::PickMicDevice) {
            PickMicDevice(button.listIndex);
            return;
        }
        if (!ActionNeedsHold(button.action)) {
            ExecuteAction(button.action);
            return;
        }
        BeginHold(button.action);
    }

    std::optional<Button> HitButtonAtRelayCursor() const {
        if (!RelayingMouse() || mode_ != RelayMode::Menu) {
            return std::nullopt;
        }
        const float uiWidth = UiAreaWidth();
        const float uiHeight = UiAreaHeight();
        const float uiX = Clamp((virtualX_ / MenuTargetWidth()) * uiWidth, 0.0f, uiWidth - 1.0f);
        const float uiY = Clamp((virtualY_ / MenuTargetHeight()) * uiHeight, 0.0f, uiHeight - 1.0f);
        return HitButtonAtUi(uiX, uiY);
    }

    void HandleKey(SDL_Keycode key) {
        if (key == SDLK_F11) {
            const bool isFullscreen = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowFullscreen(window_, !isFullscreen);
            return;
        }
        if (discovering_) {
            if (key == SDLK_ESCAPE) {
                ExecuteAction(UiAction::Quit);
            } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                ExecuteAction(UiAction::EnterIp);
            }
            return;
        }
        if (pickingMic_) {
            if (key == SDLK_ESCAPE) {
                ExecuteAction(UiAction::Resume);
            }
            return;
        }
        if (pickingSet_) {
            if (key == SDLK_1) {
                ExecuteAction(UiAction::PickMouse);
            } else if (key == SDLK_2 && micCapable_) {
                ExecuteAction(UiAction::PickMic);
            } else if (key == SDLK_3 && micCapable_) {
                ExecuteAction(UiAction::PickBoth);
            } else if (key == SDLK_ESCAPE && hostSet_) {
                ExecuteAction(UiAction::Resume);
            }
            return;
        }
        if (enteringIp_) {
            if (connecting_) {
                if (key == SDLK_ESCAPE) {
                    ExecuteAction(UiAction::CancelIp);
                }
                return;
            }

            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                TryConnect();
            } else if (key == SDLK_BACKSPACE && !ipBuffer_.empty()) {
                ipBuffer_.pop_back();
            } else if (key == SDLK_ESCAPE) {
                ExecuteAction(UiAction::CancelIp);
            }
            return;
        }

        if (key == SDLK_ESCAPE) {
            if (menuOpen_) {
                ResumeRelay();
            } else {
                OpenRelayMenu();
            }
        } else if (key == SDLK_F3) {
            ExecuteAction(UiAction::ChangeIp);
        } else if (key == SDLK_F6) {
            if (MicEnabled()) {
                ExecuteAction(UiAction::ToggleMic);
            }
        } else if (key == SDLK_F7) {
            ExecuteAction(UiAction::ToggleGameAudio);
        } else if (key == SDLK_F8) {
            ExecuteAction(UiAction::Quit);
        } else if (key == SDLK_F9) {
            if (MouseEnabled()) {
                ExecuteAction(UiAction::ToggleMode);
            }
        } else if (key == SDLK_F10) {
            g_packetLog.Toggle();
            if (g_packetLog.Enabled()) {
                g_packetLog.Mark("logging started host=" + host_ + " mode=" + ModeName(mode_));
            }
        }
    }

    void HandleMouseButton(const SDL_Event& event) {
        std::optional<Button> hit;
        std::optional<Button> relayCursorButton;
        if (!RelayingMouse() || mode_ == RelayMode::Menu) {
            hit = HitButton(event.button.x, event.button.y);
            if (!hit && RelayingMouse()) {
                relayCursorButton = HitButtonAtRelayCursor();
            }
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (hit) {
                OnButtonPressed(*hit);
                return;
            }
            if (relayCursorButton) {
                OnButtonPressed(*relayCursorButton);
                return;
            }
            if (!RelayingMouse()) {
                return;
            }
            const int index = ButtonIndex(event.button.button);
            if (index >= 0) {
                buttonState_[static_cast<size_t>(index)] = 1;
            }
            return;
        }

        CancelHold();
        if (!RelayingMouse()) {
            return;
        }
        const int index = ButtonIndex(event.button.button);
        if (index >= 0) {
            buttonState_[static_cast<size_t>(index)] = 0;
        }
    }

    void HandleFingerButton(const SDL_Event& event) {
        const float x = event.tfinger.x * static_cast<float>(windowWidth_);
        const float y = event.tfinger.y * static_cast<float>(windowHeight_);
        const std::optional<Button> hit = HitButton(x, y);
        const SDL_FingerID finger = event.tfinger.fingerID;

        if (event.type == SDL_EVENT_FINGER_DOWN) {
            if (!hit) {
                return;
            }

            if (hit->backspace || !hit->inputText.empty()) {
                ExecuteButtonRelease(*hit);
                return;
            }

            if (hit->mouseButtonIndex >= 0 && hit->mouseButtonIndex < static_cast<int>(buttonState_.size())) {
                const size_t index = static_cast<size_t>(hit->mouseButtonIndex);
                buttonState_[index] = 1;
                touchButtonFingers_[index] = finger;
                return;
            }

            if (hit->scrollStep != 0.0f) {
                pendingScroll_ += hit->scrollStep;
                touchScrollFinger_ = finger;
                touchScrollStep_ = hit->scrollStep;
                lastTouchScrollRepeat_ = NowSeconds();
                return;
            }

            if (hit->action == UiAction::PickMicDevice && !hit->disabled) {
                PickMicDevice(hit->listIndex);
                return;
            }

            if (hit->action != UiAction::None && !hit->disabled) {
                if (ActionNeedsHold(hit->action)) {
                    touchActionFinger_ = finger;
                    touchAction_ = hit->action;
                    BeginHold(hit->action);
                } else {
                    ExecuteAction(hit->action);
                }
            }
            return;
        }

        if (event.type == SDL_EVENT_FINGER_UP) {
            for (size_t i = 0; i < touchButtonFingers_.size(); ++i) {
                if (touchButtonFingers_[i] && *touchButtonFingers_[i] == finger) {
                    buttonState_[i] = 0;
                    touchButtonFingers_[i].reset();
                    return;
                }
            }

            if (touchScrollFinger_ && *touchScrollFinger_ == finger) {
                touchScrollFinger_.reset();
                touchScrollStep_ = 0.0f;
                return;
            }

            if (touchActionFinger_ && *touchActionFinger_ == finger) {
                if (touchAction_ == UiAction::ToggleMic && holdAction_ == UiAction::ToggleMic) {
                    micHintUntil_ = NowSeconds() + 1.4;
                }
                touchActionFinger_.reset();
                touchAction_ = UiAction::None;
                CancelHold();
            }
        }
    }

    bool IsTouchControlFinger(SDL_FingerID finger) const {
        if (touchActionFinger_ && *touchActionFinger_ == finger) {
            return true;
        }
        if (touchScrollFinger_ && *touchScrollFinger_ == finger) {
            return true;
        }
        for (const std::optional<SDL_FingerID>& buttonFinger : touchButtonFingers_) {
            if (buttonFinger && *buttonFinger == finger) {
                return true;
            }
        }
        return false;
    }

    void AppendIpText(const char* text) {
        for (const char* p = text; p && *p; ++p) {
            if ((*p >= '0' && *p <= '9') || *p == '.') {
                if (ipBuffer_.size() < 15) {
                    ipBuffer_.push_back(*p);
                }
            }
        }
    }

    void StartDiscovery() {
        finder_.Cancel();
        discovering_ = true;
        pickingSet_ = false;
        enteringIp_ = false;
        connecting_ = false;
        menuOpen_ = false;
        reconnecting_ = false;
        ipError_.clear();
        SDL_StopTextInput(window_);
        SetMouseCapture(false);
        finder_.Start(host_.empty() ? LoadSavedIp() : host_);
    }

    void PollFinder() {
        const std::optional<FinderResult> result = finder_.TakeResult();
        if (!result) {
            return;
        }
        micCapable_ = result->micCapable;
        if (discovering_) {
            discovering_ = false;
            autoConnect_ = true;
            BeginConnect(result->ip);
            return;
        }
        if (reconnecting_) {
            if (result->ip != host_) {
                std::string error;
                if (transport_.Open(result->ip, error)) {
                    host_ = result->ip;
                    SaveIp(host_);
                    mic_.SetHost(host_);
                    gameAudio_.SetHost(host_);
                    lastSentButtons_.fill(-1);
                    lastRxTime_ = NowSeconds();
                }
            }
        }
    }

    void TryConnect() {
        std::string candidate = ipBuffer_;
        candidate.erase(std::remove_if(candidate.begin(), candidate.end(), [](unsigned char ch) {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        }), candidate.end());

        if (!IsValidIpv4(candidate)) {
            ipError_ = "Not a valid IPv4 address";
            return;
        }

        autoConnect_ = false;
        BeginConnect(candidate);
    }

    void BeginConnect(const std::string& candidate) {
        std::string error;
        if (!transport_.Open(candidate, error)) {
            hostSet_ = false;
            host_.clear();
            ipError_ = error;
            enteringIp_ = true;
            BeginIpTextInput();
            return;
        }

        hostSet_ = false;
        host_.clear();
        pendingHost_ = candidate;
        connecting_ = true;
        enteringIp_ = true;
        connected_ = false;
        lastStatus_ = "<none>";
        statusPackets_ = 0;
        sentPackets_ = 0;
        pendingScroll_ = 0.0f;
        accumDx_ = 0.0f;
        accumDy_ = 0.0f;
        motionPending_ = false;
        lastSentButtons_.fill(-1);
        ipError_ = "Checking for Bandit launcher at " + candidate + "...";
        connectStart_ = NowSeconds();
        lastConnectProbe_ = 0.0;

        SDL_StopTextInput(window_);
    }

    void RefreshWindowSize() {
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(window_, &width, &height) || width <= 0 || height <= 0) {
            width = 1280;
            height = 720;
        }

        windowWidth_ = width;
        windowHeight_ = height;

        safeArea_ = SDL_Rect{ 0, 0, width, height };
        if (kTouchLayout) {
            SDL_Rect safe{};
            // notch and home indicator sit over the ui otherwise
            if (SDL_GetWindowSafeArea(window_, &safe) && safe.w > 0 && safe.h > 0) {
                safeArea_ = safe;
            }
        }

        pixelDensity_ = SDL_GetWindowPixelDensity(window_);
        if (!(pixelDensity_ > 0.0f)) {
            pixelDensity_ = 1.0f;
        }

        RefreshMotionScale();
    }

    void BeginIpTextInput() {
        // touch has its own keypad, ios would pop the system keyboard over the ui
        if (!kTouchLayout) {
            SDL_StartTextInput(window_);
        }
    }

    void SetMouseCapture(bool on) {
        SDL_SetWindowRelativeMouseMode(window_, on);
        SDL_SetWindowMouseGrab(window_, on);
    }

    void OpenRelayMenu() {
        ReleaseAllButtons();
        CancelHold();
        pendingScroll_ = 0.0f;
        accumDx_ = 0.0f;
        accumDy_ = 0.0f;
        motionPending_ = false;
        menuOpen_ = true;
        SetMouseCapture(false);
    }

    void ResumeRelay() {
        if (enteringIp_ || discovering_) {
            return;
        }
        if (pickingMic_) {
            LeaveMicPicker();
            return;
        }
        if (pickingSet_) {
            if (!hostSet_) {
                return;
            }
            pickingSet_ = false;
        }
        menuOpen_ = false;
        if (WantMouseCapture()) {
            SetMouseCapture(true);
        }
    }

    void ChooseSet(RelaySet set) {
        set_ = set;
        setChosen_ = true;
        SaveSet(set_);
        pickingSet_ = false;
        menuOpen_ = false;
        if (!MicEnabled() && micOn_) {
            StopMic();
        }
        if (MicEnabled() && !micDeviceChosen_) {
            OpenMicPicker(MicPickReturn::Relay);
            return;
        }
        ApplySet();
    }

    void ApplySet() {
        if (set_ == RelaySet::Mic && !micOn_) {
            StartMic();
        }
        if (!MouseEnabled()) {
            ReleaseAllButtons();
            SetMouseCapture(false);
        } else if (WantMouseCapture()) {
            SetMouseCapture(true);
        }
        ApplyGameAudio();
    }

    void EnterRelay() {
        pickingSet_ = false;
        menuOpen_ = false;
        if (MicEnabled() && !micDeviceChosen_) {
            OpenMicPicker(MicPickReturn::Relay);
            return;
        }
        ApplySet();
    }

    void OpenMicPicker(MicPickReturn back) {
        ReleaseAllButtons();
        CancelHold();
        micList_ = MicSender::DeviceNames();
        micPickReturn_ = back;
        pickingMic_ = true;
        menuOpen_ = false;
        SetMouseCapture(false);
    }

    void PickMicDevice(int index) {
        if (index < 0 || index >= static_cast<int>(micList_.size())) {
            return;
        }
        micDevice_ = micList_[static_cast<size_t>(index)];
        micDeviceChosen_ = true;
        SavePref("mic_device.txt", micDevice_);
        micNote_.clear();
        if (micOn_) {
            StartMic();
        }
        LeaveMicPicker();
    }

    void LeaveMicPicker() {
        pickingMic_ = false;
        if (!hostSet_) {
            return;
        }
        if (micPickReturn_ == MicPickReturn::Menu) {
            menuOpen_ = true;
            return;
        }
        ApplySet();
    }

    void ApplyGameAudio() {
        if (gameAudioWanted_ && hostSet_) {
            if (!gameAudio_.Running()) {
                std::string error;
                if (!gameAudio_.Start(host_, error)) {
                    micNote_ = error;
                    gameAudioWanted_ = false;
                }
            }
        } else if (gameAudio_.Running()) {
            gameAudio_.Stop();
        }
    }

    void StartMic() {
        if (!MicEnabled() || !hostSet_) {
            return;
        }
#ifdef SDL_PLATFORM_ANDROID
        if (!androidMicGranted_) {
            if (!androidMicAsked_) {
                androidMicAsked_ = true;
                micNote_ = "Waiting for microphone permission";
                SDL_RequestAndroidPermission("android.permission.RECORD_AUDIO", &RelayApp::OnAndroidPermission, this);
            }
            return;
        }
#endif
        std::string error;
        if (!mic_.Start(host_, micDevice_, error)) {
            micNote_ = error;
            micOn_ = false;
            return;
        }
        micOn_ = true;
        micNote_ = mic_.FellBack() ? "Saved mic not found, using the system default" : std::string();
    }

    void StopMic() {
        mic_.Stop();
        micOn_ = false;
    }

#ifdef SDL_PLATFORM_ANDROID
    static void SDLCALL OnAndroidPermission(void* userdata, const char*, bool granted) {
        RelayApp* app = static_cast<RelayApp*>(userdata);
        app->androidMicResult_ = granted ? 1 : 2;
    }
#endif

    void PollMicPermission() {
#ifdef SDL_PLATFORM_ANDROID
        const int result = androidMicResult_.exchange(0);
        if (result == 1) {
            androidMicGranted_ = true;
            micNote_.clear();
            if (MicEnabled() && !micOn_) {
                StartMic();
            }
        } else if (result == 2) {
            androidMicAsked_ = false;
            micNote_ = "Microphone permission denied";
        }
#endif
    }

    void AddMotion(float rawDx, float rawDy) {
        if (mode_ == RelayMode::Menu) {
            const float dx = rawDx * MenuScaleX();
            const float dy = rawDy * MenuScaleY();
            if (dx != 0.0f || dy != 0.0f) {
                virtualX_ = Clamp(virtualX_ + dx, 0.0f, MenuTargetWidth() - 1.0f);
                virtualY_ = Clamp(virtualY_ + dy, 0.0f, MenuTargetHeight() - 1.0f);
                motionPending_ = true;
            }
        } else if (rawDx != 0.0f || rawDy != 0.0f) {
            accumDx_ += rawDx * scaleX_;
            accumDy_ += rawDy * scaleY_;
            motionPending_ = true;
        }
    }

    void PollStatus() {
        if ((!connecting_ && enteringIp_) || !transport_.IsOpen()) {
            return;
        }

        for (const std::string& status : transport_.ReceiveStatus()) {
            ProcessStatus(status);
        }
    }

    void ProcessStatus(const std::string& status) {
        const auto sync = ParseSyncStatus(status);
        const auto windowCursor = ParsePairStatus(status, "cursorw=");
        const auto protocolCursor = ParsePairStatus(status, "cursor=");
        const auto size = ParseSizeStatus(status);
        const auto menuSize = ParseDimensionStatus(status, "menu=");
        RelayMode parsedMode{};
        const bool hasMode = ParseModeStatus(status, parsedMode);
        const bool isReady = status.rfind(kReadyPrefix, 0) == 0 ||
            status.rfind("javauwp_glfw_mouse:receiving", 0) == 0;
        if (!sync && !windowCursor && !protocolCursor && !hasMode && !size && !menuSize && !isReady) {
            return;
        }

        if (isReady && StatusHasMic(status)) {
            micCapable_ = true;
        }

        if (connecting_) {
            CompleteConnection(status);
        }

        ++statusPackets_;
        connected_ = true;
        lastRxTime_ = NowSeconds();
        if (reconnecting_) {
            reconnecting_ = false;
            finder_.Cancel();
        }
        lastStatus_ = status;

        if (size) {
            SetTargetSize(size->first, size->second);
        }
        if (menuSize) {
            SetMenuTargetSize(menuSize->first, menuSize->second);
        }

        if (sync) {
            float syncX = sync->first;
            float syncY = sync->second;
            if (status.rfind("SYNC:", 0) == 0) {
                syncX = syncX * (MenuTargetWidth() / kTargetWidth);
                syncY = syncY * (MenuTargetHeight() / kTargetHeight);
            } else if (status.rfind("SYNCW:", 0) == 0) {
                syncX = WindowToMenuX(syncX);
                syncY = WindowToMenuY(syncY);
            }
            SyncMenuPos(syncX, syncY, true);
            return;
        }

        if (mode_ == RelayMode::Menu) {
            if (windowCursor) {
                SyncMenuPos(
                    WindowToMenuX(windowCursor->first),
                    WindowToMenuY(windowCursor->second),
                    true);
            } else if (protocolCursor) {
                SyncMenuPos(
                    protocolCursor->first * (MenuTargetWidth() / kTargetWidth),
                    protocolCursor->second * (MenuTargetHeight() / kTargetHeight),
                    true);
            }
        }

        if (hasMode) {
            const RelayMode previous = mode_;
            appMode_ = parsedMode;
            mode_ = parsedMode;
            if (previous == RelayMode::Gameplay && mode_ == RelayMode::Menu) {
                SyncMenuCenter();
            }
        }
    }

    void CompleteConnection(const std::string&) {
        host_ = pendingHost_;
        pendingHost_.clear();
        hostSet_ = true;
        SaveIp(host_);
        connecting_ = false;
        enteringIp_ = false;
        menuOpen_ = false;
        ipError_.clear();
        SDL_StopTextInput(window_);
        mic_.SetHost(host_);
        gameAudio_.SetHost(host_);
        if (!setChosen_ || (MicEnabled() && !micCapable_)) {
            pickingSet_ = true;
            SetMouseCapture(false);
            return;
        }
        EnterRelay();
    }

    void CheckLink() {
        if (!OnRelayScreen()) {
            return;
        }
        const double now = NowSeconds();
        if (reconnecting_) {
            if (!finder_.Running() && now - reconnectStamp_ >= 3.0) {
                reconnectStamp_ = now;
                finder_.Start(host_);
            }
            return;
        }
        if (!connected_ || now - lastRxTime_ < kLinkLostSeconds) {
            return;
        }
        connected_ = false;
        reconnecting_ = true;
        reconnectStamp_ = now;
        ReleaseAllButtons();
        finder_.Start(host_);
    }

    void SetTargetSize(float width, float height) {
        targetWidth_ = Clamp(width, 1.0f, 16384.0f);
        targetHeight_ = Clamp(height, 1.0f, 16384.0f);
        if (haveMenuTargetSize_) {
            ResizeMenuTarget(menuTargetWidth_, menuTargetHeight_, true);
        } else {
            ResizeMenuTarget(targetWidth_ * kMenuCoordinateScale, targetHeight_ * kMenuCoordinateScale, false);
        }
        RefreshMotionScale();
    }

    void SetMenuTargetSize(float width, float height) {
        ResizeMenuTarget(width, height, true);
    }

    void ResizeMenuTarget(float width, float height, bool explicitSize) {
        const float oldMenuWidth = MenuTargetWidth();
        const float oldMenuHeight = MenuTargetHeight();
        const float relativeX = oldMenuWidth > 0.0f ? virtualX_ / oldMenuWidth : 0.5f;
        const float relativeY = oldMenuHeight > 0.0f ? virtualY_ / oldMenuHeight : 0.5f;

        menuTargetWidth_ = Clamp(width, 1.0f, targetWidth_);
        menuTargetHeight_ = Clamp(height, 1.0f, targetHeight_);
        haveMenuTargetSize_ = haveMenuTargetSize_ || explicitSize;

        virtualX_ = Clamp(relativeX * MenuTargetWidth(), 0.0f, MenuTargetWidth() - 1.0f);
        virtualY_ = Clamp(relativeY * MenuTargetHeight(), 0.0f, MenuTargetHeight() - 1.0f);
    }

    void RefreshMotionScale() {
        scaleX_ = targetWidth_ / static_cast<float>(std::max(1, windowWidth_));
        scaleY_ = targetHeight_ / static_cast<float>(std::max(1, windowHeight_));
    }

    float MenuTargetWidth() const {
        return Clamp(menuTargetWidth_, 1.0f, targetWidth_);
    }

    float MenuTargetHeight() const {
        return Clamp(menuTargetHeight_, 1.0f, targetHeight_);
    }

    float MenuScaleX() const {
        return MenuTargetWidth() / static_cast<float>(std::max(1, windowWidth_));
    }

    float MenuScaleY() const {
        return MenuTargetHeight() / static_cast<float>(std::max(1, windowHeight_));
    }

    static float MapCoordinate(float value, float sourceExtent, float targetExtent) {
        if (sourceExtent <= 1.0f || targetExtent <= 1.0f) {
            return 0.0f;
        }
        const float clamped = Clamp(value, 0.0f, sourceExtent - 1.0f);
        return clamped * ((targetExtent - 1.0f) / (sourceExtent - 1.0f));
    }

    float MenuToWindowX(float x) const {
        return MapCoordinate(x, MenuTargetWidth(), targetWidth_);
    }

    float MenuToWindowY(float y) const {
        return MapCoordinate(y, MenuTargetHeight(), targetHeight_);
    }

    float WindowToMenuX(float x) const {
        return MapCoordinate(x, targetWidth_, MenuTargetWidth());
    }

    float WindowToMenuY(float y) const {
        return MapCoordinate(y, targetHeight_, MenuTargetHeight());
    }

    void SendConnectionProbe() {
        if (!connecting_ || !transport_.IsOpen()) {
            return;
        }

        const double now = NowSeconds();
        if (now - connectStart_ >= kConnectTimeoutSeconds) {
            const std::string failedHost = pendingHost_;
            connecting_ = false;
            pendingHost_.clear();
            transport_.Close();
            if (autoConnect_) {
                StartDiscovery();
                return;
            }
            ipError_ = "No Bandit launcher response from " + failedHost;
            BeginIpTextInput();
            return;
        }

        if (lastConnectProbe_ == 0.0 || now - lastConnectProbe_ >= kConnectProbeSeconds) {
            if (transport_.Send("ping")) {
                ++sentPackets_;
            }
            lastConnectProbe_ = now;
        }
    }

    void RepeatHeldTouchScroll() {
        if (!touchScrollFinger_ || touchScrollStep_ == 0.0f || !RelayingMouse()) {
            return;
        }

        const double now = NowSeconds();
        if (now - lastTouchScrollRepeat_ >= kTouchScrollRepeatSeconds) {
            pendingScroll_ += touchScrollStep_;
            lastTouchScrollRepeat_ = now;
        }
    }

    std::string FormatPacket(float dx, float dy, const std::array<int, 5>& buttons, float scroll) const {
        char buffer[160]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%.4f,%.4f,%d,%d,%d,%.4f,%d,%d",
            dx,
            dy,
            buttons[0],
            buttons[1],
            buttons[2],
            scroll,
            buttons[3],
            buttons[4]);
        return buffer;
    }

    std::string FormatAbsPacket(float x, float y, const std::array<int, 5>& buttons, float scroll) const {
        char buffer[180]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "ABSW:%.4f,%.4f,%d,%d,%d,%.4f,%d,%d",
            MenuToWindowX(x),
            MenuToWindowY(y),
            buttons[0],
            buttons[1],
            buttons[2],
            scroll,
            buttons[3],
            buttons[4]);
        return buffer;
    }

    void SendPendingPackets() {
        if (!hostSet_ || enteringIp_ || !transport_.IsOpen()) {
            return;
        }

        const double now = NowSeconds();
        if (MouseEnabled()) {
            std::array<int, 5> packetButtons{ -1, -1, -1, -1, -1 };
            bool haveButtonChange = false;

            for (size_t i = 0; i < buttonState_.size(); ++i) {
                if (buttonState_[i] != lastSentButtons_[i]) {
                    packetButtons[i] = buttonState_[i];
                    haveButtonChange = true;
                }
            }

            // no time gate, the loop already ticks at this interval and a late wake doubles up
            const bool motionDue = motionPending_;
            const bool heldRefreshDue = AnyLocalMouseButtonDown() && (now - lastHeldButtonRefresh_) >= kHeldButtonRefreshSeconds;
            if (heldRefreshDue) {
                packetButtons = buttonState_;
                haveButtonChange = true;
            }

            if (haveButtonChange || pendingScroll_ != 0.0f || motionDue) {
                const float scroll = pendingScroll_;
                pendingScroll_ = 0.0f;

                const std::string packet = mode_ == RelayMode::Menu
                    ? FormatAbsPacket(virtualX_, virtualY_, packetButtons, scroll)
                    : FormatPacket(accumDx_, accumDy_, packetButtons, scroll);

                if (transport_.Send(packet)) {
                    ++sentPackets_;
                }

                accumDx_ = 0.0f;
                accumDy_ = 0.0f;
                motionPending_ = false;
                if (lastSendStamp_ > 0.0) {
                    const double dtMs = (now - lastSendStamp_) * 1000.0;
                    sendDtMinMs_ = std::min(sendDtMinMs_, dtMs);
                    sendDtMaxMs_ = std::max(sendDtMaxMs_, dtMs);
                }
                lastSendStamp_ = now;
                if (heldRefreshDue) {
                    lastHeldButtonRefresh_ = now;
                }

                for (size_t i = 0; i < packetButtons.size(); ++i) {
                    if (packetButtons[i] != -1) {
                        lastSentButtons_[i] = packetButtons[i];
                    }
                }
            }
        }

        if (now - lastPing_ >= 1.0) {
            if (transport_.Send("ping")) {
                ++sentPackets_;
            }
            lastPing_ = now;
        }
    }

    bool AnyLocalMouseButtonDown() const {
        for (int state : buttonState_) {
            if (state) {
                return true;
            }
        }
        return false;
    }

    void SyncMenuCenter() {
        SyncMenuPos(MenuTargetWidth() * 0.5f, MenuTargetHeight() * 0.5f, false);
    }

    void SyncMenuPos(float x, float y, bool fromStatus) {
        virtualX_ = Clamp(x, 0.0f, MenuTargetWidth() - 1.0f);
        virtualY_ = Clamp(y, 0.0f, MenuTargetHeight() - 1.0f);
        accumDx_ = 0.0f;
        accumDy_ = 0.0f;
        motionPending_ = false;

        if (!fromStatus && transport_.IsOpen()) {
            const std::array<int, 5> unchanged{ -1, -1, -1, -1, -1 };
            if (transport_.Send(FormatAbsPacket(virtualX_, virtualY_, unchanged, 0.0f))) {
                ++sentPackets_;
            }
        }
    }

    void ReleaseAllButtons() {
        buttonState_.fill(0);
        for (std::optional<SDL_FingerID>& buttonFinger : touchButtonFingers_) {
            buttonFinger.reset();
        }
        touchScrollFinger_.reset();
        touchScrollStep_ = 0.0f;
        if (transport_.IsOpen() && MouseEnabled()) {
            const std::array<int, 5> released{ 0, 0, 0, 0, 0 };
            if (transport_.Send(FormatPacket(0.0f, 0.0f, released, 0.0f))) {
                ++sentPackets_;
            }
            lastSentButtons_ = released;
        }
    }

    std::string FitText(const std::string& text, float width) const {
        const size_t limit = static_cast<size_t>(std::max(4.0f, width / kDebugGlyphWidth));
        return ShortName(text, limit);
    }

    static std::string ShortName(const std::string& name, size_t limit) {
        if (name.size() <= limit) {
            return name;
        }
        return name.substr(0, limit - 1) + "~";
    }

    void UpdateLevels() {
        const float micRaw = micOn_ ? mic_.Level() : 0.0f;
        micLevel_ = std::max(micRaw, micLevel_ * 0.86f);
        if (micLevel_ < 0.004f) {
            micLevel_ = 0.0f;
        }
        const float gameRaw = gameAudio_.Playing() ? gameAudio_.Level() : 0.0f;
        gameLevel_ = std::max(gameRaw, gameLevel_ * 0.86f);
        if (gameLevel_ < 0.004f) {
            gameLevel_ = 0.0f;
        }
    }

    void DrawText(float x, float y, const std::string& text, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        SDL_RenderDebugText(renderer_, x, y, text.c_str());
    }

    float TextWidth(const std::string& text) const {
        return static_cast<float>(text.size()) * kDebugGlyphWidth;
    }

    void DrawTextCentered(float centerX, float y, const std::string& text, SDL_Color color) {
        DrawText(centerX - (TextWidth(text) * 0.5f), y, text, color);
    }

    void DrawRing(float centerX, float centerY, float radius, SDL_Color color) {
        constexpr int kSegments = 64;
        std::array<SDL_FPoint, kSegments + 1> points{};
        for (int i = 0; i <= kSegments; ++i) {
            const float angle = (static_cast<float>(i) / static_cast<float>(kSegments)) * 6.2831853f;
            points[static_cast<size_t>(i)] = SDL_FPoint{ centerX + std::cos(angle) * radius, centerY + std::sin(angle) * radius };
        }
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        SDL_RenderLines(renderer_, points.data(), static_cast<int>(points.size()));
    }

    void DrawSlash(float centerX, float centerY, float radius, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        const float d = radius * 0.7f;
        SDL_RenderLine(renderer_, centerX - d, centerY - d, centerX + d, centerY + d);
        SDL_RenderLine(renderer_, centerX - d + 1.0f, centerY - d, centerX + d + 1.0f, centerY + d);
    }

    void DrawBars(float centerX, float centerY, float maxHeight, float level, bool live, SDL_Color color) {
        constexpr int kBars = 5;
        const float barWidth = std::max(3.0f, maxHeight * 0.12f);
        const float gap = barWidth * 0.7f;
        const float total = (barWidth * kBars) + (gap * (kBars - 1));
        const float t = static_cast<float>(NowSeconds());
        const std::array<float, kBars> weights{ 0.45f, 0.8f, 1.0f, 0.8f, 0.45f };
        for (int i = 0; i < kBars; ++i) {
            float height = maxHeight * 0.12f;
            if (live) {
                const float wobble = 0.5f + (0.5f * std::sin((t * 9.0f) + (static_cast<float>(i) * 1.7f)));
                height += maxHeight * level * weights[static_cast<size_t>(i)] * (0.7f + (0.3f * wobble));
                if (level <= 0.0f) {
                    height += maxHeight * 0.04f * (0.5f + (0.5f * std::sin((t * 2.2f) + static_cast<float>(i))));
                }
            }
            height = Clamp(height, 2.0f, maxHeight);
            const SDL_FRect bar{ centerX - (total * 0.5f) + (static_cast<float>(i) * (barWidth + gap)), centerY - (height * 0.5f), barWidth, height };
            SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
            SDL_RenderFillRect(renderer_, &bar);
        }
    }

    void DrawMicIndicator(float centerX, float centerY, float radius) {
        const bool live = micOn_ && mic_.Running();
        const float t = static_cast<float>(NowSeconds());
        if (live) {
            const float breathe = 0.5f + (0.5f * std::sin(t * 2.0f));
            const float grow = radius * (0.08f * breathe + (0.55f * micLevel_));
            DrawRing(centerX, centerY, radius + grow, SDL_Color{ 147, 221, 232, static_cast<Uint8>(90 + (120.0f * micLevel_)) });
            DrawRing(centerX, centerY, radius + (grow * 1.8f) + 4.0f, SDL_Color{ 105, 183, 204, static_cast<Uint8>(30 + (90.0f * micLevel_)) });
            DrawRing(centerX, centerY, radius, SDL_Color{ 147, 221, 232, 255 });
            DrawBars(centerX, centerY, radius * 1.1f, micLevel_, true, SDL_Color{ 232, 246, 250, 255 });
        } else {
            DrawRing(centerX, centerY, radius, SDL_Color{ 96, 108, 124, 255 });
            DrawBars(centerX, centerY, radius * 1.1f, 0.0f, false, SDL_Color{ 96, 108, 124, 255 });
            DrawSlash(centerX, centerY, radius, SDL_Color{ 255, 118, 118, 255 });
        }
    }

    void DrawButton(const Button& button) {
        const bool active = (button.mouseButtonIndex >= 0 &&
            button.mouseButtonIndex < static_cast<int>(buttonState_.size()) &&
            buttonState_[static_cast<size_t>(button.mouseButtonIndex)] != 0) ||
            (button.scrollStep != 0.0f && touchScrollStep_ == button.scrollStep) ||
            (button.action != UiAction::None && touchAction_ == button.action) ||
            (button.action == UiAction::ToggleMic && micOn_ && !menuOpen_);

        if (button.disabled) {
            SDL_SetRenderDrawColor(renderer_, 20, 26, 34, 255);
        } else if (active) {
            SDL_SetRenderDrawColor(renderer_, 58, 92, 112, 255);
        } else {
            SDL_SetRenderDrawColor(renderer_, 32, 43, 58, 255);
        }
        SDL_RenderFillRect(renderer_, &button.rect);

        const float holdProgress = HoldProgress(button);
        if (holdProgress > 0.0f) {
            SDL_FRect fill = button.rect;
            fill.w = button.rect.w * holdProgress;
            SDL_SetRenderDrawColor(renderer_, 80, 140, 165, 255);
            SDL_RenderFillRect(renderer_, &fill);
        }

        if (button.disabled) {
            SDL_SetRenderDrawColor(renderer_, 60, 72, 86, 255);
        } else {
            SDL_SetRenderDrawColor(renderer_, active ? 147 : 105, active ? 221 : 183, active ? 232 : 204, 255);
        }
        SDL_RenderRect(renderer_, &button.rect);

        if (!button.label.empty()) {
            const float textX = button.rect.x + ((button.rect.w - TextWidth(button.label)) * 0.5f);
            const float textY = button.rect.y + ((button.rect.h - kDebugGlyphHeight) * 0.5f);
            DrawText(textX, textY, button.label, button.disabled ? SDL_Color{ 110, 122, 138, 255 } : SDL_Color{ 232, 246, 250, 255 });
        }
    }

    float DiscoverTop(float uiHeight) const {
        return std::max(8.0f, (uiHeight - 200.0f) * 0.5f);
    }

    float MicPickTop(float uiHeight) const {
        const float rows = static_cast<float>(std::min<size_t>(std::max<size_t>(micList_.size(), 1), 8));
        const float height = kTouchLayout ? 44.0f : 34.0f;
        const float gap = kTouchLayout ? 10.0f : 8.0f;
        const float contentHeight = 52.0f + (rows * (height + gap)) + 12.0f + height;
        return std::max(8.0f, (uiHeight - contentHeight) * 0.5f);
    }

    void RenderMicPickScreen(float uiWidth, float uiHeight) {
        const float centerX = uiWidth * 0.5f;
        const float top = MicPickTop(uiHeight);
        DrawTextCentered(centerX, top, "Choose your microphone", SDL_Color{ 246, 249, 252, 255 });
        DrawTextCentered(centerX, top + 22.0f, "You can change this later from the menu", SDL_Color{ 145, 158, 174, 255 });
        for (const Button& button : BuildButtons(uiWidth, uiHeight)) {
            DrawButton(button);
            if (button.action == UiAction::PickMicDevice && button.listIndex >= 0 &&
                button.listIndex < static_cast<int>(micList_.size()) && micList_[static_cast<size_t>(button.listIndex)] == micDevice_) {
                SDL_SetRenderDrawColor(renderer_, 147, 221, 232, 255);
                SDL_RenderRect(renderer_, &button.rect);
                DrawText(button.rect.x + 10.0f, button.rect.y + ((button.rect.h - kDebugGlyphHeight) * 0.5f), ">", SDL_Color{ 147, 221, 232, 255 });
            }
        }
    }

    float PickTop(float uiHeight) const {
        const float contentHeight = kTouchLayout ? 260.0f : 230.0f;
        return std::max(8.0f, (uiHeight - contentHeight) * 0.5f);
    }

    float IpScreenTop(float uiHeight) const {
        // touch runs title down to the bottom of Connect, 374 + 46
        const float contentHeight = kTouchLayout ? 420.0f : 216.0f;
        return std::max(8.0f, (uiHeight - contentHeight) * 0.5f);
    }

    float MicRingRadius(float uiHeight) const {
        return Clamp(uiHeight * 0.16f, 44.0f, 96.0f);
    }

    float MicCenterY(float uiHeight) const {
        return (uiHeight * 0.5f) - 10.0f;
    }

    void RenderDiscoverScreen(float uiWidth, float uiHeight) {
        const float centerX = uiWidth * 0.5f;
        const float top = DiscoverTop(uiHeight);

        DrawTextCentered(centerX, top, "Looking for Xbox (make sure bandit launcher is turned on)", SDL_Color{ 246, 249, 252, 255 });
        DrawTextCentered(centerX, top + 36.0f, "sit tight while it works it's magic :3", SDL_Color{ 145, 158, 174, 255 });

        const float barWidth = std::min(380.0f, std::max(300.0f, uiWidth - 64.0f));
        const SDL_FRect bar{ centerX - (barWidth * 0.5f), top + 70.0f, barWidth, 10.0f };
        SDL_SetRenderDrawColor(renderer_, 16, 22, 31, 255);
        SDL_RenderFillRect(renderer_, &bar);
        const float t = static_cast<float>(NowSeconds());
        const float sweep = 0.5f + (0.5f * std::sin(t * 2.6f));
        const float highlightWidth = barWidth * 0.28f;
        const SDL_FRect highlight{ bar.x + ((barWidth - highlightWidth) * sweep), bar.y, highlightWidth, bar.h };
        SDL_SetRenderDrawColor(renderer_, 105, 183, 204, 255);
        SDL_RenderFillRect(renderer_, &highlight);
        SDL_SetRenderDrawColor(renderer_, 58, 92, 112, 255);
        SDL_RenderRect(renderer_, &bar);

        for (const Button& button : BuildButtons(uiWidth, uiHeight)) {
            DrawButton(button);
        }
    }

    void RenderPickScreen(float uiWidth, float uiHeight) {
        const float centerX = uiWidth * 0.5f;
        const float top = PickTop(uiHeight);

        DrawTextCentered(centerX, top, "Bandit Relay", SDL_Color{ 246, 249, 252, 255 });
        DrawTextCentered(centerX, top + 24.0f, "What do you want to relay?", SDL_Color{ 218, 229, 241, 255 });

        const std::vector<Button> buttons = BuildButtons(uiWidth, uiHeight);
        for (const Button& button : buttons) {
            DrawButton(button);
        }

        for (const Button& button : buttons) {
            if (button.action == UiAction::PickBoth) {
                DrawRecommended(button.rect);
            }
        }

        if (!micCapable_ && !buttons.empty()) {
            DrawTextCentered(centerX, buttons.front().rect.y + buttons.front().rect.h + 18.0f, "This Xbox build has no mic relay yet, update Bandit Launcher to use it", SDL_Color{ 252, 213, 128, 255 });
        }
    }

    static SDL_FPoint PerimeterPoint(const SDL_FRect& rect, float distance) {
        const float perimeter = 2.0f * (rect.w + rect.h);
        float d = std::fmod(distance, perimeter);
        if (d < 0.0f) {
            d += perimeter;
        }
        if (d < rect.w) {
            return SDL_FPoint{ rect.x + d, rect.y };
        }
        d -= rect.w;
        if (d < rect.h) {
            return SDL_FPoint{ rect.x + rect.w, rect.y + d };
        }
        d -= rect.h;
        if (d < rect.w) {
            return SDL_FPoint{ rect.x + rect.w - d, rect.y + rect.h };
        }
        d -= rect.w;
        return SDL_FPoint{ rect.x, rect.y + rect.h - d };
    }

    void DrawRecommended(const SDL_FRect& rect) {
        const float t = static_cast<float>(NowSeconds());
        const float pulse = 0.5f + (0.5f * std::sin(t * 2.6f));
        const SDL_Color gold{ 232, 190, 60, 255 };

        for (int i = 4; i >= 1; --i) {
            const float grow = static_cast<float>(i) * 2.0f;
            const SDL_FRect halo{ rect.x - grow, rect.y - grow, rect.w + (grow * 2.0f), rect.h + (grow * 2.0f) };
            const Uint8 alpha = static_cast<Uint8>((70.0f - (static_cast<float>(i) * 14.0f)) * (0.55f + (0.45f * pulse)));
            SDL_SetRenderDrawColor(renderer_, gold.r, gold.g, gold.b, alpha);
            SDL_RenderRect(renderer_, &halo);
        }

        SDL_SetRenderDrawColor(renderer_, gold.r, gold.g, gold.b, 255);
        SDL_RenderRect(renderer_, &rect);

        const SDL_FRect edge{ rect.x - 1.0f, rect.y - 1.0f, rect.w + 2.0f, rect.h + 2.0f };
        const float perimeter = 2.0f * (edge.w + edge.h);
        const float head = std::fmod(t * 0.38f, 1.0f) * perimeter;
        const float tail = perimeter * 0.22f;
        constexpr int kSteps = 28;
        SDL_FPoint previous = PerimeterPoint(edge, head - tail);
        for (int i = 1; i <= kSteps; ++i) {
            const float along = static_cast<float>(i) / static_cast<float>(kSteps);
            const SDL_FPoint point = PerimeterPoint(edge, head - tail + (tail * along));
            const Uint8 alpha = static_cast<Uint8>(40.0f + (215.0f * along));
            SDL_SetRenderDrawColor(renderer_, 255, 232, 150, alpha);
            SDL_RenderLine(renderer_, previous.x, previous.y, point.x, point.y);
            previous = point;
        }

        DrawTextCentered(rect.x + (rect.w * 0.5f), rect.y - 20.0f, "Recommended", gold);
    }

    void RenderIpScreen(float uiWidth, float uiHeight) {
        const float centerX = uiWidth * 0.5f;
        const float top = IpScreenTop(uiHeight);
        const float inputWidth = std::min(380.0f, std::max(300.0f, uiWidth - 64.0f));

        DrawTextCentered(centerX, top, "Bandit Relay", SDL_Color{ 246, 249, 252, 255 });
        DrawTextCentered(centerX, top + 32.0f, "Enter the Xbox Developer Mode IP address", SDL_Color{ 174, 187, 202, 255 });

        const SDL_FRect box{ centerX - (inputWidth * 0.5f), top + 78.0f, inputWidth, 34.0f };
        SDL_SetRenderDrawColor(renderer_, 16, 22, 31, 255);
        SDL_RenderFillRect(renderer_, &box);
        SDL_SetRenderDrawColor(renderer_, 105, 183, 204, 255);
        SDL_RenderRect(renderer_, &box);

        const bool showCaret = (SDL_GetTicks() / 500) % 2 == 0;
        DrawText(box.x + 12.0f, box.y + 13.0f, ipBuffer_ + (showCaret ? "_" : ""), SDL_Color{ 222, 245, 250, 255 });

        if (!ipError_.empty()) {
            const SDL_Color color = connecting_ ? SDL_Color{ 252, 213, 128, 255 } : SDL_Color{ 255, 118, 118, 255 };
            DrawTextCentered(centerX, box.y - 24.0f, ipError_, color);
        }

        const std::string hint = connecting_
            ? "Waiting for the launcher to answer on UDP " + std::to_string(kStatusPort) + ". Esc cancels."
            : (kTouchLayout ? "Use the keypad below, then tap Connect" : "Keyboard: Enter confirm, Backspace edit, Esc back");
        DrawTextCentered(centerX, box.y + 50.0f, hint, SDL_Color{ 145, 158, 174, 255 });

        for (const Button& button : BuildButtons(uiWidth, uiHeight)) {
            DrawButton(button);
        }
    }

    void RenderMenuPanel(float uiWidth, float uiHeight) {
        const float panelHeight = MenuPanelHeight();
        const float panelWidth = 460.0f;
        const SDL_FRect panel{ (uiWidth - panelWidth) * 0.5f, (uiHeight - panelHeight) * 0.5f, panelWidth, panelHeight };
        SDL_SetRenderDrawColor(renderer_, 13, 18, 26, 235);
        SDL_RenderFillRect(renderer_, &panel);
        SDL_SetRenderDrawColor(renderer_, 105, 183, 204, 255);
        SDL_RenderRect(renderer_, &panel);
        DrawTextCentered(uiWidth * 0.5f, panel.y + 22.0f, "Relay Menu", SDL_Color{ 246, 249, 252, 255 });
        if (MicEnabled()) {
            DrawTextCentered(uiWidth * 0.5f, panel.y + 44.0f, FitText("Microphone: " + (mic_.Running() ? mic_.ActiveName() : micDevice_), panelWidth - 24.0f), SDL_Color{ 174, 187, 202, 255 });
        }
        for (const Button& button : BuildButtons(uiWidth, uiHeight)) {
            DrawButton(button);
        }
    }

    std::string LinkText() const {
        if (reconnecting_) {
            return "RECONNECTING";
        }
        return connected_ ? "CONNECTED" : "WAITING";
    }

    void RenderMicScreen(float uiWidth, float uiHeight) {
        const float centerX = uiWidth * 0.5f;
        const float centerY = MicCenterY(uiHeight);
        const float radius = MicRingRadius(uiHeight);

        DrawText(18.0f, 18.0f, "Bandit Relay", SDL_Color{ 246, 249, 252, 255 });
        DrawText(18.0f, 42.0f, "Xbox: " + host_ + "  Link: " + LinkText(), SDL_Color{ 218, 229, 241, 255 });
        DrawText(18.0f, 62.0f, "Mic: " + ShortName(mic_.Running() ? mic_.ActiveName() : micDevice_, 40), SDL_Color{ 174, 187, 202, 255 });
        DrawText(18.0f, 82.0f, "Packets: " + std::to_string(mic_.Packets()) + "  Game audio: " + (gameAudio_.Playing() ? "playing" : (gameAudioWanted_ ? "waiting" : "off")), SDL_Color{ 174, 187, 202, 255 });
        if (!micNote_.empty()) {
            DrawText(18.0f, 102.0f, micNote_, SDL_Color{ 252, 213, 128, 255 });
        }

        DrawMicIndicator(centerX, centerY, radius);

        const std::string state = micOn_ ? "MIC ON" : "MIC OFF";
        DrawTextCentered(centerX, centerY + radius + 30.0f, state, micOn_ ? SDL_Color{ 147, 221, 232, 255 } : SDL_Color{ 174, 187, 202, 255 });
        const std::string hint = kTouchLayout
            ? (micOn_ ? "Tap the ring to mute" : "Tap the ring to unmute")
            : (micOn_ ? "Click the ring or press F6 to mute. Esc opens the menu" : "Click the ring or press F6 to unmute. Esc opens the menu");
        DrawTextCentered(centerX, centerY + radius + 50.0f, hint, SDL_Color{ 145, 158, 174, 255 });

        if (gameAudioWanted_) {
            DrawBars(uiWidth - 60.0f, 40.0f, 28.0f, gameLevel_, gameAudio_.Playing(), SDL_Color{ 150, 205, 165, 255 });
            DrawTextCentered(uiWidth - 60.0f, 62.0f, "GAME", SDL_Color{ 150, 205, 165, 255 });
        }

        if (menuOpen_) {
            RenderMenuPanel(uiWidth, uiHeight);
            return;
        }

        for (const Button& button : BuildButtons(uiWidth, uiHeight)) {
            if (button.action == UiAction::ToggleMic) {
                continue;
            }
            DrawButton(button);
        }
    }

    void RenderRelayScreen(float uiWidth, float uiHeight) {
        const std::string appMode = appMode_ ? ModeName(*appMode_) : "UNKNOWN";

        DrawText(18.0f, 18.0f, "Bandit Relay", SDL_Color{ 246, 249, 252, 255 });
        DrawText(18.0f, 42.0f, "Xbox: " + host_, SDL_Color{ 218, 229, 241, 255 });
        DrawText(18.0f, 62.0f, "Mouse: " + ModeName(mode_) + "  App: " + appMode + "  UDP: " + LinkText(), SDL_Color{ 218, 229, 241, 255 });
        DrawText(18.0f, 82.0f, "Packets: sent=" + std::to_string(sentPackets_.load()) + " status=" + std::to_string(statusPackets_), SDL_Color{ 174, 187, 202, 255 });
        DrawText(18.0f, 102.0f, "Window: " + std::to_string(windowWidth_) + "x" + std::to_string(windowHeight_) + " -> menu " + std::to_string((int)MenuTargetWidth()) + "x" + std::to_string((int)MenuTargetHeight()) + " raw " + std::to_string((int)targetWidth_) + "x" + std::to_string((int)targetHeight_), SDL_Color{ 174, 187, 202, 255 });
        DrawText(18.0f, 122.0f, kTouchLayout ? "Touch: drag empty space, hold L/R with another finger, hold the mic pad to toggle" : "Keys: Esc menu, F3 change IP, F6 mic, F7 game audio, F8 quit, F9 toggle local mode, F10 packet log", SDL_Color{ 145, 158, 174, 255 });
        DrawText(18.0f, 142.0f, "Last: " + lastStatus_, SDL_Color{ 145, 158, 174, 255 });

        int diagPixelW = 0;
        int diagPixelH = 0;
        SDL_GetWindowSizeInPixels(window_, &diagPixelW, &diagPixelH);
        const bool diagRelative = SDL_GetWindowRelativeMouseMode(window_);
        const bool diagGrab = SDL_GetWindowMouseGrab(window_);
        const bool diagFocus = (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
        char diagLine[240];
        std::snprintf(diagLine, sizeof(diagLine),
            "Capture: rel=%s grab=%s focus=%s  pixels=%dx%d  dpr=%.2f  safe=%dx%d+%d+%d  ui=%.0fx%.0f @%.2f",
            diagRelative ? "on" : "off", diagGrab ? "on" : "off", diagFocus ? "yes" : "no",
            diagPixelW, diagPixelH, SDL_GetWindowDisplayScale(window_),
            safeArea_.w, safeArea_.h, safeArea_.x, safeArea_.y,
            UiAreaWidth(), UiAreaHeight(), UiScale());
        DrawText(18.0f, 162.0f, diagLine, SDL_Color{ 150, 205, 165, 255 });

        char sendLine[120];
        std::snprintf(sendLine, sizeof(sendLine), "Send dt(ms): min=%.1f max=%.1f (even ~4.2 = no render starvation)", sendDtMinMs_, sendDtMaxMs_);
        DrawText(18.0f, 182.0f, sendLine, SDL_Color{ 205, 195, 150, 255 });
        sendDtMinMs_ = 1000.0;
        sendDtMaxMs_ = 0.0;

        char logLine[256];
        if (g_packetLog.Enabled()) {
            std::snprintf(logLine, sizeof(logLine), "Packet log: ON -> %s", g_packetLog.Path().c_str());
        } else {
            std::snprintf(logLine, sizeof(logLine), "Packet log: off (press F10 to record sent/received packets)");
        }
        DrawText(18.0f, 202.0f, logLine, g_packetLog.Enabled() ? SDL_Color{ 235, 180, 90, 255 } : SDL_Color{ 145, 158, 174, 255 });

        std::string audioLine = "Game audio: " + std::string(gameAudio_.Playing() ? "playing" : (gameAudioWanted_ ? "waiting for the game" : "off"));
        if (!micNote_.empty()) {
            audioLine += "  " + micNote_;
        }
        DrawText(18.0f, 222.0f, audioLine, SDL_Color{ 145, 158, 174, 255 });
        if (MicEnabled()) {
            DrawText(18.0f, 242.0f, FitText("Mic: " + (mic_.Running() ? mic_.ActiveName() : micDevice_), uiWidth - 36.0f), SDL_Color{ 147, 221, 232, 255 });
        }

        if (MicEnabled()) {
            const SDL_FRect pad = MicPadRect(uiWidth);
            const float ringX = pad.x - 40.0f;
            const float ringY = pad.y + (pad.h * 0.5f);
            DrawMicIndicator(ringX, ringY, 18.0f);
            if (!kTouchLayout) {
                DrawText(pad.x, pad.y + 10.0f, micOn_ ? "MIC ON" : "MIC OFF", micOn_ ? SDL_Color{ 147, 221, 232, 255 } : SDL_Color{ 174, 187, 202, 255 });
                DrawText(pad.x, pad.y + 28.0f, micOn_ ? "F6 mutes" : "F6 unmutes", SDL_Color{ 145, 158, 174, 255 });
            } else if (NowSeconds() < micHintUntil_ && !menuOpen_) {
                DrawText(pad.x - 8.0f, pad.y + pad.h + 8.0f, "Hold to toggle", SDL_Color{ 252, 213, 128, 255 });
            }
        }

        if (gameAudioWanted_) {
            const float x = MicEnabled() ? uiWidth - 240.0f : uiWidth - 60.0f;
            DrawBars(x, 40.0f, 28.0f, gameLevel_, gameAudio_.Playing(), SDL_Color{ 150, 205, 165, 255 });
        }

        if (menuOpen_) {
            RenderMenuPanel(uiWidth, uiHeight);
        } else {
            for (const Button& button : BuildButtons(uiWidth, uiHeight)) {
                DrawButton(button);
            }
        }

        if (mode_ == RelayMode::Menu && MouseEnabled()) {
            const float x = Clamp((virtualX_ / MenuTargetWidth()) * uiWidth, 0.0f, uiWidth - 1.0f);
            const float y = Clamp((virtualY_ / MenuTargetHeight()) * uiHeight, 0.0f, uiHeight - 1.0f);
            SDL_SetRenderDrawColor(renderer_, 245, 247, 250, 255);
            SDL_RenderLine(renderer_, x - 9.0f, y, x + 9.0f, y);
            SDL_RenderLine(renderer_, x, y - 9.0f, x, y + 9.0f);
            SDL_SetRenderDrawColor(renderer_, 9, 12, 18, 255);
            SDL_RenderPoint(renderer_, x, y);
        }
    }

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    UdpTransport transport_;
    XboxFinder finder_;
    MicSender mic_;
    GameAudioPlayer gameAudio_;

    bool running_ = true;
    bool discovering_ = false;
    bool pickingSet_ = false;
    bool enteringIp_ = false;
    bool menuOpen_ = false;
    bool connecting_ = false;
    bool hostSet_ = false;
    bool connected_ = false;
    bool reconnecting_ = false;
    bool autoConnect_ = true;
    bool micCapable_ = false;
    bool setChosen_ = false;
    bool micOn_ = false;
    bool pickingMic_ = false;
    bool micDeviceChosen_ = false;
    MicPickReturn micPickReturn_ = MicPickReturn::Relay;
    std::vector<std::string> micList_;
    bool gameAudioWanted_ = false;
    RelaySet set_ = RelaySet::Mouse;
    std::string micDevice_;
    std::string micNote_;
    float micLevel_ = 0.0f;
    float gameLevel_ = 0.0f;
    double micHintUntil_ = 0.0;
    double lastRxTime_ = 0.0;
    double reconnectStamp_ = 0.0;
#ifdef SDL_PLATFORM_ANDROID
    bool androidMicGranted_ = false;
    bool androidMicAsked_ = false;
    std::atomic<int> androidMicResult_{ 0 };
#endif

    int windowWidth_ = 1280;
    int windowHeight_ = 720;
    SDL_Rect safeArea_{ 0, 0, 1280, 720 };
    float pixelDensity_ = 1.0f;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    float targetWidth_ = kTargetWidth;
    float targetHeight_ = kTargetHeight;
    float menuTargetWidth_ = kTargetWidth * kMenuCoordinateScale;
    float menuTargetHeight_ = kTargetHeight * kMenuCoordinateScale;
    bool haveMenuTargetSize_ = false;

    std::string ipBuffer_;
    std::string ipError_;
    std::string host_;
    std::string pendingHost_;
    std::string lastStatus_ = "<none>";

    RelayMode mode_ = RelayMode::Menu;
    std::optional<RelayMode> appMode_;
    std::array<int, 5> buttonState_{ 0, 0, 0, 0, 0 };
    std::array<int, 5> lastSentButtons_{ -1, -1, -1, -1, -1 };
    std::array<std::optional<SDL_FingerID>, 5> touchButtonFingers_{};
    std::optional<SDL_FingerID> touchActionFinger_;
    std::optional<SDL_FingerID> touchScrollFinger_;
    UiAction touchAction_ = UiAction::None;
    UiAction holdAction_ = UiAction::None;
    double holdStart_ = 0.0;

    float virtualX_ = kTargetWidth * kMenuCoordinateScale * 0.5f;
    float virtualY_ = kTargetHeight * kMenuCoordinateScale * 0.5f;
    float accumDx_ = 0.0f;
    float accumDy_ = 0.0f;
    float pendingScroll_ = 0.0f;
    float touchScrollStep_ = 0.0f;
    bool motionPending_ = false;
    double connectStart_ = 0.0;
    double lastConnectProbe_ = 0.0;
    double lastHeldButtonRefresh_ = 0.0;
    double lastTouchScrollRepeat_ = 0.0;
    double lastPing_ = 0.0;
    double lastSendStamp_ = 0.0;
    double sendDtMinMs_ = 1000.0;
    double sendDtMaxMs_ = 0.0;
    std::atomic<uint64_t> sentPackets_{ 0 };
    uint64_t statusPackets_ = 0;

    std::mutex sendMutex_;
    std::thread netThread_;
    std::atomic<bool> netRunning_{ false };
};

} // namespace

#ifdef _WIN32
static void RaiseRelaySchedulingPriority() {
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    PROCESS_POWER_THROTTLING_STATE throttling{};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
}
#endif

int main(int, char**) {
    WinsockRuntime winsock;
    if (!winsock.ok) {
        SDL_Log("Winsock initialization failed");
        return 2;
    }

    TimerResolution timerResolution;

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (kTouchLayout) {
        // android gets this from the activity instead
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 2;
    }

    if (kTouchLayout) {
        // relay takes no touches mid-game so the idle timer would lock the phone
        SDL_DisableScreenSaver();
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(
            "Bandit Relay",
            960,
            540,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &window,
            &renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    SDL_SetRenderVSync(renderer, 0);

#ifdef _WIN32
    RaiseRelaySchedulingPriority();
#endif

    {
        RelayApp app(window, renderer);
        double lastRender = -kRenderIntervalSeconds;
        double nextTick = NowSeconds();
        while (app.Running()) {
            app.PumpAndTick();
            const double now = NowSeconds();
            if (now - lastRender >= kRenderIntervalSeconds) {
                app.Render();
                lastRender = now;
            }

            nextTick += kSendIntervalSeconds;
            if (nextTick < now) {
                nextTick = now;
            }
            SleepUntil(nextTick);
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
