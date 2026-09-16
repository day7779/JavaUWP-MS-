#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

using Clock = std::chrono::steady_clock;

static std::atomic<bool> gRunning{true};

static void Log(const char* format, ...) noexcept
{
    char message[1024]{};

    va_list arguments;
    va_start(arguments, format);
    _vsnprintf_s(message, sizeof(message), _TRUNCATE, format, arguments);
    va_end(arguments);

    FILE* file = nullptr;
    if (fopen_s(&file, "audio_relay.log", "ab") != 0 || file == nullptr)
        return;

    SYSTEMTIME time{};
    GetLocalTime(&time);

    std::fprintf(
        file,
        "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds,
        message);

    std::fclose(file);
}

class SparseErrorLog
{
public:
    explicit SparseErrorLog(const char* label) noexcept
        : label_(label)
    {
    }

    void Record(unsigned long code) noexcept
    {
        ++pending_;
        const auto now = Clock::now();

        if (!hasLogged_ || now >= nextLog_)
        {
            Log(
                "%s: %llu failure(s), last code 0x%08lX",
                label_,
                static_cast<unsigned long long>(pending_),
                code);

            pending_ = 0;
            hasLogged_ = true;
            nextLog_ = now + std::chrono::seconds(10);
        }
    }

private:
    const char* label_;
    unsigned long long pending_ = 0;
    bool hasLogged_ = false;
    Clock::time_point nextLog_{};
};

static BOOL WINAPI ConsoleHandler(DWORD type)
{
    switch (type)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        gRunning.store(false);
        return TRUE;
    default:
        return FALSE;
    }
}

static std::string EndpointText(const sockaddr_in& address)
{
    char host[INET_ADDRSTRLEN]{};
    if (InetNtopA(AF_INET, &address.sin_addr, host, sizeof(host)) == nullptr)
        strcpy_s(host, "unknown");

    char result[64]{};
    _snprintf_s(
        result,
        sizeof(result),
        _TRUNCATE,
        "%s:%u",
        host,
        static_cast<unsigned>(ntohs(address.sin_port)));

    return result;
}

static bool SameEndpoint(const sockaddr_in& left, const sockaddr_in& right) noexcept
{
    return left.sin_family == right.sin_family &&
           left.sin_port == right.sin_port &&
           left.sin_addr.s_addr == right.sin_addr.s_addr;
}

enum class SampleEncoding
{
    Float32,
    Pcm16,
    Pcm24,
    Pcm32
};

struct MixInfo
{
    uint32_t sampleRate = 0;
    uint16_t inputChannels = 0;
    uint8_t outputChannels = 0;
    uint16_t bitsPerSample = 0;
    uint16_t bytesPerSample = 0;
    uint16_t blockAlign = 0;
    SampleEncoding encoding = SampleEncoding::Pcm16;
};

static const GUID kSubtypePcm = {
    0x00000001,
    0x0000,
    0x0010,
    {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

static const GUID kSubtypeFloat = {
    0x00000003,
    0x0000,
    0x0010,
    {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

static bool DescribeMixFormat(const WAVEFORMATEX* format, MixInfo& result) noexcept
{
    if (format == nullptr ||
        format->nSamplesPerSec == 0 ||
        format->nChannels == 0 ||
        format->nBlockAlign == 0)
    {
        return false;
    }

    WORD tag = format->wFormatTag;

    if (tag == WAVE_FORMAT_EXTENSIBLE)
    {
        if (format->cbSize <
            sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        {
            return false;
        }

        const auto* extensible =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);

        if (IsEqualGUID(extensible->SubFormat, kSubtypePcm))
            tag = WAVE_FORMAT_PCM;
        else if (IsEqualGUID(extensible->SubFormat, kSubtypeFloat))
            tag = WAVE_FORMAT_IEEE_FLOAT;
        else
            return false;
    }

    result.sampleRate = format->nSamplesPerSec;
    result.inputChannels = format->nChannels;
    result.outputChannels =
        static_cast<uint8_t>(std::min<uint16_t>(format->nChannels, 2));
    result.bitsPerSample = format->wBitsPerSample;
    result.blockAlign = format->nBlockAlign;

    if (tag == WAVE_FORMAT_IEEE_FLOAT && format->wBitsPerSample == 32)
    {
        result.encoding = SampleEncoding::Float32;
        result.bytesPerSample = 4;
    }
    else if (tag == WAVE_FORMAT_PCM && format->wBitsPerSample == 16)
    {
        result.encoding = SampleEncoding::Pcm16;
        result.bytesPerSample = 2;
    }
    else if (tag == WAVE_FORMAT_PCM && format->wBitsPerSample == 24)
    {
        result.encoding = SampleEncoding::Pcm24;
        result.bytesPerSample = 3;
    }
    else if (tag == WAVE_FORMAT_PCM && format->wBitsPerSample == 32)
    {
        result.encoding = SampleEncoding::Pcm32;
        result.bytesPerSample = 4;
    }
    else
    {
        return false;
    }

    const uint32_t requiredAlign =
        static_cast<uint32_t>(result.inputChannels) *
        result.bytesPerSample;

    return result.blockAlign >= requiredAlign;
}

static const char* EncodingText(SampleEncoding encoding) noexcept
{
    switch (encoding)
    {
    case SampleEncoding::Float32:
        return "float";
    case SampleEncoding::Pcm16:
        return "PCM";
    case SampleEncoding::Pcm24:
        return "PCM";
    case SampleEncoding::Pcm32:
        return "PCM";
    default:
        return "unknown";
    }
}

static int16_t ReadSample(const BYTE* source, SampleEncoding encoding) noexcept
{
    switch (encoding)
    {
    case SampleEncoding::Float32:
    {
        float value = 0.0f;
        std::memcpy(&value, source, sizeof(value));

        if (std::isnan(value))
            value = 0.0f;

        value = std::clamp(value, -1.0f, 1.0f);
        return static_cast<int16_t>(value * 32767.0f);
    }

    case SampleEncoding::Pcm16:
    {
        int16_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return value;
    }

    case SampleEncoding::Pcm24:
    {
        int32_t value =
            static_cast<int32_t>(source[0]) |
            (static_cast<int32_t>(source[1]) << 8) |
            (static_cast<int32_t>(source[2]) << 16);

        if ((value & 0x00800000) != 0)
            value -= 0x01000000;

        return static_cast<int16_t>(value >> 8);
    }

    case SampleEncoding::Pcm32:
    {
        int32_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return static_cast<int16_t>(value >> 16);
    }

    default:
        return 0;
    }
}

static void WriteU16(BYTE* destination, uint16_t value) noexcept
{
    destination[0] = static_cast<BYTE>(value);
    destination[1] = static_cast<BYTE>(value >> 8);
}

static void WriteU32(BYTE* destination, uint32_t value) noexcept
{
    destination[0] = static_cast<BYTE>(value);
    destination[1] = static_cast<BYTE>(value >> 8);
    destination[2] = static_cast<BYTE>(value >> 16);
    destination[3] = static_cast<BYTE>(value >> 24);
}

class LoopbackCapture
{
public:
    ~LoopbackCapture()
    {
        Close();
    }

    bool IsOpen() const noexcept
    {
        return captureClient_ != nullptr && started_;
    }

    HRESULT Open()
    {
        Close();

        HRESULT result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator_));

        if (FAILED(result))
        {
            Close();
            return result;
        }

        result = enumerator_->GetDefaultAudioEndpoint(
            eRender,
            eConsole,
            &device_);

        if (FAILED(result))
        {
            Close();
            return result;
        }

        result = device_->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(&audioClient_));

        if (FAILED(result))
        {
            Close();
            return result;
        }

        result = audioClient_->GetMixFormat(&waveFormat_);
        if (FAILED(result))
        {
            Close();
            return result;
        }

        if (!DescribeMixFormat(waveFormat_, mix_))
        {
            Close();
            return AUDCLNT_E_UNSUPPORTED_FORMAT;
        }

        constexpr REFERENCE_TIME bufferDuration = 1000000;

        result = audioClient_->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK,
            bufferDuration,
            0,
            waveFormat_,
            nullptr);

        if (FAILED(result))
        {
            Close();
            return result;
        }

        result = audioClient_->GetService(
            __uuidof(IAudioCaptureClient),
            reinterpret_cast<void**>(&captureClient_));

        if (FAILED(result))
        {
            Close();
            return result;
        }

        result = audioClient_->Start();
        if (FAILED(result))
        {
            Close();
            return result;
        }

        started_ = true;

        Log(
            "format: %lu Hz, %u input channel(s), %u output channel(s), %s %u-bit to s16le",
            static_cast<unsigned long>(mix_.sampleRate),
            static_cast<unsigned>(mix_.inputChannels),
            static_cast<unsigned>(mix_.outputChannels),
            EncodingText(mix_.encoding),
            static_cast<unsigned>(mix_.bitsPerSample));

        return S_OK;
    }

    void Close() noexcept
    {
        if (audioClient_ != nullptr && started_)
            audioClient_->Stop();

        started_ = false;

        if (captureClient_ != nullptr)
        {
            captureClient_->Release();
            captureClient_ = nullptr;
        }

        if (audioClient_ != nullptr)
        {
            audioClient_->Release();
            audioClient_ = nullptr;
        }

        if (device_ != nullptr)
        {
            device_->Release();
            device_ = nullptr;
        }

        if (enumerator_ != nullptr)
        {
            enumerator_->Release();
            enumerator_ = nullptr;
        }

        if (waveFormat_ != nullptr)
        {
            CoTaskMemFree(waveFormat_);
            waveFormat_ = nullptr;
        }

        mix_ = {};
    }

    HRESULT Pump(
        SOCKET socketHandle,
        const sockaddr_in& destination,
        uint32_t& sequence,
        SparseErrorLog& sendErrors)
    {
        if (!IsOpen())
            return E_UNEXPECTED;

        UINT32 availableFrames = 0;
        HRESULT result =
            captureClient_->GetNextPacketSize(&availableFrames);

        if (FAILED(result))
            return result;

        while (availableFrames > 0)
        {
            BYTE* source = nullptr;
            UINT32 frameCount = 0;
            DWORD flags = 0;

            result = captureClient_->GetBuffer(
                &source,
                &frameCount,
                &flags,
                nullptr,
                nullptr);

            if (result == AUDCLNT_S_BUFFER_EMPTY)
                break;

            if (FAILED(result))
                return result;

            const bool silent =
                (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

            UINT32 frameOffset = 0;

            while (frameOffset < frameCount)
            {
                constexpr UINT32 maxPacketFrames = 240;
                constexpr size_t headerSize = 16;
                BYTE packet[headerSize + maxPacketFrames * 2 * 2]{};

                const UINT32 chunkFrames = std::min(
                    maxPacketFrames,
                    frameCount - frameOffset);

                std::memcpy(packet, "BMA2", 4);
                WriteU32(packet + 4, sequence++);
                WriteU32(packet + 8, mix_.sampleRate);
                packet[12] = mix_.outputChannels;
                packet[13] = 16;
                WriteU16(
                    packet + 14,
                    static_cast<uint16_t>(chunkFrames));

                size_t outputOffset = headerSize;

                for (UINT32 frame = 0; frame < chunkFrames; ++frame)
                {
                    const UINT32 inputFrame = frameOffset + frame;

                    for (uint8_t channel = 0;
                         channel < mix_.outputChannels;
                         ++channel)
                    {
                        int16_t sample = 0;

                        if (!silent && source != nullptr)
                        {
                            const BYTE* sampleSource =
                                source +
                                static_cast<size_t>(inputFrame) *
                                    mix_.blockAlign +
                                static_cast<size_t>(channel) *
                                    mix_.bytesPerSample;

                            sample = ReadSample(
                                sampleSource,
                                mix_.encoding);
                        }

                        packet[outputOffset++] =
                            static_cast<BYTE>(sample);
                        packet[outputOffset++] =
                            static_cast<BYTE>(
                                static_cast<uint16_t>(sample) >> 8);
                    }
                }

                const int sent = sendto(
                    socketHandle,
                    reinterpret_cast<const char*>(packet),
                    static_cast<int>(outputOffset),
                    0,
                    reinterpret_cast<const sockaddr*>(&destination),
                    sizeof(destination));

                if (sent == SOCKET_ERROR)
                {
                    sendErrors.Record(
                        static_cast<unsigned long>(WSAGetLastError()));
                }

                frameOffset += chunkFrames;
            }

            result = captureClient_->ReleaseBuffer(frameCount);
            if (FAILED(result))
                return result;

            result =
                captureClient_->GetNextPacketSize(&availableFrames);

            if (FAILED(result))
                return result;
        }

        return S_OK;
    }

private:
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* audioClient_ = nullptr;
    IAudioCaptureClient* captureClient_ = nullptr;
    WAVEFORMATEX* waveFormat_ = nullptr;
    MixInfo mix_{};
    bool started_ = false;
};

struct Subscriber
{
    bool active = false;
    sockaddr_in address{};
    Clock::time_point lastSeen{};
};

static bool DrainControl(
    SOCKET socketHandle,
    Subscriber& subscriber,
    SparseErrorLog& receiveErrors)
{
    bool changed = false;

    for (;;)
    {
        char data[64]{};
        sockaddr_in source{};
        int sourceLength = sizeof(source);

        const int received = recvfrom(
            socketHandle,
            data,
            sizeof(data),
            0,
            reinterpret_cast<sockaddr*>(&source),
            &sourceLength);

        if (received == SOCKET_ERROR)
        {
            const int error = WSAGetLastError();

            if (error == WSAECONNRESET)
                continue;

            if (error != WSAEWOULDBLOCK)
                receiveErrors.Record(static_cast<unsigned long>(error));
            break;
        }

        if (received != 4 || std::memcmp(data, "BMAS", 4) != 0)
            continue;

        const auto now = Clock::now();

        if (!subscriber.active ||
            !SameEndpoint(subscriber.address, source))
        {
            if (subscriber.active)
            {
                const std::string previous =
                    EndpointText(subscriber.address);
                Log("unsubscribe %s (replaced)", previous.c_str());
            }

            subscriber.active = true;
            subscriber.address = source;
            changed = true;

            const std::string current = EndpointText(source);
            Log("subscribe %s", current.c_str());
        }

        subscriber.lastSeen = now;
    }

    return changed;
}

static void WaitForControl(
    SOCKET socketHandle,
    long timeoutMilliseconds,
    SparseErrorLog& selectErrors)
{
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socketHandle, &readSet);

    timeval timeout{};
    timeout.tv_sec = timeoutMilliseconds / 1000;
    timeout.tv_usec =
        (timeoutMilliseconds % 1000) * 1000;

    const int result = select(
        0,
        &readSet,
        nullptr,
        nullptr,
        &timeout);

    if (result == SOCKET_ERROR)
    {
        selectErrors.Record(
            static_cast<unsigned long>(WSAGetLastError()));
    }
}

static bool ParsePort(
    int argumentCount,
    wchar_t** arguments,
    uint16_t& port) noexcept
{
    port = 42734;

    if (argumentCount == 1)
        return true;

    if (argumentCount != 2)
        return false;

    wchar_t* end = nullptr;
    const unsigned long value =
        std::wcstoul(arguments[1], &end, 10);

    if (end == arguments[1] ||
        *end != L'\0' ||
        value == 0 ||
        value > 65535)
    {
        return false;
    }

    port = static_cast<uint16_t>(value);
    return true;
}

int wmain(int argumentCount, wchar_t** arguments)
{
    uint16_t controlPort = 42734;

    if (!ParsePort(argumentCount, arguments, controlPort))
    {
        Log("invalid arguments; expected optional control port");
        return 2;
    }

    Log("startup: control port %u", static_cast<unsigned>(controlPort));

    WSADATA winsockData{};
    const int winsockResult =
        WSAStartup(MAKEWORD(2, 2), &winsockData);

    if (winsockResult != 0)
    {
        Log("WSAStartup failed: %d", winsockResult);
        return 3;
    }

    const HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (FAILED(comResult))
    {
        Log(
            "CoInitializeEx failed: 0x%08lX",
            static_cast<unsigned long>(comResult));
        WSACleanup();
        return 4;
    }

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    const SOCKET socketHandle =
        socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (socketHandle == INVALID_SOCKET)
    {
        Log("socket failed: %d", WSAGetLastError());
        CoUninitialize();
        WSACleanup();
        return 5;
    }

    sockaddr_in bindAddress{};
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddress.sin_port = htons(controlPort);

    if (bind(
            socketHandle,
            reinterpret_cast<const sockaddr*>(&bindAddress),
            sizeof(bindAddress)) == SOCKET_ERROR)
    {
        Log("bind failed: %d", WSAGetLastError());
        closesocket(socketHandle);
        CoUninitialize();
        WSACleanup();
        return 6;
    }

    u_long nonBlocking = 1;
    if (ioctlsocket(
            socketHandle,
            FIONBIO,
            &nonBlocking) == SOCKET_ERROR)
    {
        Log("ioctlsocket failed: %d", WSAGetLastError());
        closesocket(socketHandle);
        CoUninitialize();
        WSACleanup();
        return 7;
    }

    BOOL reportConnReset = FALSE;
    DWORD ioctlBytes = 0;

    if (WSAIoctl(
            socketHandle,
            SIO_UDP_CONNRESET,
            &reportConnReset,
            sizeof(reportConnReset),
            nullptr,
            0,
            &ioctlBytes,
            nullptr,
            nullptr) == SOCKET_ERROR)
    {
        Log(
            "SIO_UDP_CONNRESET disable failed: %d",
            WSAGetLastError());
    }

    Subscriber subscriber{};
    LoopbackCapture capture;
    uint32_t sequence = 0;
    auto nextAcquire = Clock::now();

    SparseErrorLog receiveErrors("control receive");
    SparseErrorLog selectErrors("control wait");
    SparseErrorLog sendErrors("audio send");
    SparseErrorLog acquireErrors(
        "capture acquire; retry scheduled");
    SparseErrorLog captureErrors(
        "capture failure; reacquire scheduled");

    while (gRunning.load())
    {
        const bool subscriberChanged =
            DrainControl(
                socketHandle,
                subscriber,
                receiveErrors);

        auto now = Clock::now();

        if (subscriberChanged && !capture.IsOpen())
        {
            Log("reacquire default render endpoint");
            nextAcquire = now;
        }

        if (!subscriber.active)
        {
            capture.Close();
            WaitForControl(
                socketHandle,
                1000,
                selectErrors);
            continue;
        }

        if (now - subscriber.lastSeen >=
            std::chrono::seconds(6))
        {
            const std::string endpoint =
                EndpointText(subscriber.address);

            Log("unsubscribe %s (keepalive timeout)", endpoint.c_str());

            subscriber.active = false;
            capture.Close();
            continue;
        }

        if (!capture.IsOpen())
        {
            if (now >= nextAcquire)
            {
                const HRESULT result = capture.Open();

                if (FAILED(result))
                {
                    acquireErrors.Record(
                        static_cast<unsigned long>(result));
                    nextAcquire =
                        now + std::chrono::seconds(1);
                }
            }

            if (!capture.IsOpen())
            {
                WaitForControl(
                    socketHandle,
                    50,
                    selectErrors);
                continue;
            }
        }

        const HRESULT pumpResult = capture.Pump(
            socketHandle,
            subscriber.address,
            sequence,
            sendErrors);

        if (FAILED(pumpResult))
        {
            captureErrors.Record(
                static_cast<unsigned long>(pumpResult));

            capture.Close();
            nextAcquire =
                Clock::now() + std::chrono::seconds(1);

            WaitForControl(
                socketHandle,
                50,
                selectErrors);
            continue;
        }

        Sleep(5);
    }

    if (subscriber.active)
    {
        const std::string endpoint =
            EndpointText(subscriber.address);
        Log("unsubscribe %s (shutdown)", endpoint.c_str());
    }

    capture.Close();
    closesocket(socketHandle);
    CoUninitialize();
    WSACleanup();

    Log("shutdown");
    return 0;
}
