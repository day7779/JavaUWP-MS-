#define WIN32_LEAN_AND_MEAN
#define MOUSE_SUPPORT_EXPORTS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "mouse_support_core.h"
#include "mouse_support_api.h"

#pragma comment(lib, "ws2_32.lib")

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace {

const unsigned short kInputPort = 7331;
const unsigned short kStatusPort = 7332;
const unsigned short kOverlayPort = 7333;
const double kProtocolWidth = 1920.0;
const double kProtocolHeight = 1080.0;
const int kCursorModeNormal = 0x00034001;
const int kCursorModeDisabled = 0x00034003;
const DWORD kModeStabilizeMs = 150;

SRWLOCK g_stateLock = SRWLOCK_INIT;
SRWLOCK g_mailboxLock = SRWLOCK_INIT;
mousesupport::MouseMailbox g_mailbox;

MouseSupportHostState g_host = { 1920, 1080, 960, 540, kCursorModeDisabled, 960.0, 540.0 };

SOCKET g_statusSocket = INVALID_SOCKET;
SOCKET g_overlaySocket = INVALID_SOCKET;
sockaddr_in g_statusAddr = {};
bool g_haveStatusAddr = false;

volatile LONG g_started = 0;
volatile LONG g_shutdown = 0;
volatile LONG g_lastActivityTick = 0;
HANDLE g_receiveThread = nullptr;

int g_pendingMode = -1;
int g_reportedMode = kCursorModeDisabled;
ULONGLONG g_modeChangeTime = 0;

long long g_qpcStart = 0;
double g_qpcTicksPerMicro = 1.0;

bool g_diag = false;
long long g_lastConsumeMicros = 0;
FILE* g_diagFile = nullptr;
bool g_diagFileTried = false;

long long NowMicros() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (long long)((double)(c.QuadPart - g_qpcStart) / g_qpcTicksPerMicro);
}

void DiagOpenIfNeeded() {
    if (g_diagFile || g_diagFileTried) return;
    g_diagFileTried = true;
    char dir[512] = {};
    const DWORD n = GetEnvironmentVariableA("MC_LOG_DIR", dir, sizeof(dir));
    if (n > 0 && n < sizeof(dir)) {
        char path[640] = {};
        sprintf_s(path, "%s\\mouse_support_diag.log", dir);
        fopen_s(&g_diagFile, path, "a");
    }
}

void DiagLog(const char* fmt, ...) {
    if (!g_diag) return;
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    DiagOpenIfNeeded();
    if (g_diagFile) {
        fprintf(g_diagFile, "%s\n", line);
    } else {
        OutputDebugStringA("[ms-diag] ");
        OutputDebugStringA(line);
        OutputDebugStringA("\n");
    }
}

void Log(const char* fmt, ...) {
    char line[320];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    OutputDebugStringA("[mouse_support] ");
    OutputDebugStringA(line);
    OutputDebugStringA("\n");
}

double WindowToProtocolX(double x, int windowWidth) {
    return windowWidth > 0 ? x * (kProtocolWidth / (double)windowWidth) : x;
}

double WindowToProtocolY(double y, int windowHeight) {
    return windowHeight > 0 ? y * (kProtocolHeight / (double)windowHeight) : y;
}

int StableModeLocked() {
    if (g_pendingMode >= 0) {
        const ULONGLONG elapsed = GetTickCount64() - g_modeChangeTime;
        if (elapsed < kModeStabilizeMs) {
            return g_reportedMode;
        }
        g_reportedMode = g_pendingMode;
        g_pendingMode = -1;
    }
    return g_reportedMode;
}

void EnsureStatusSocket() {
    if (g_statusSocket == INVALID_SOCKET) {
        g_statusSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }
}

void SendStatusText(const char* text) {
    sockaddr_in target = {};
    bool haveTarget = false;
    AcquireSRWLockShared(&g_stateLock);
    if (g_haveStatusAddr) {
        target = g_statusAddr;
        haveTarget = true;
    }
    ReleaseSRWLockShared(&g_stateLock);
    if (!haveTarget) return;

    EnsureStatusSocket();
    if (g_statusSocket == INVALID_SOCKET) return;
    sendto(g_statusSocket, text, (int)strlen(text), 0,
        reinterpret_cast<const sockaddr*>(&target), sizeof(target));
}

void RememberStatusAddress(const sockaddr_in& from) {
    sockaddr_in statusTo = from;
    statusTo.sin_port = htons(kStatusPort);
    AcquireSRWLockExclusive(&g_stateLock);
    g_statusAddr = statusTo;
    g_haveStatusAddr = true;
    ReleaseSRWLockExclusive(&g_stateLock);
}

DWORD WINAPI ReceiveThreadProc(LPVOID) {
    Log("receive thread starting on UDP %u", kInputPort);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        Log("socket failed err=%d", WSAGetLastError());
        return 0;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kInputPort);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        Log("bind UDP %u failed err=%d", kInputPort, WSAGetLastError());
        closesocket(sock);
        return 0;
    }

    int rcvBuf = 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
    DWORD rcvTimeoutMs = 50;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeoutMs), sizeof(rcvTimeoutMs));

    BOOL connReset = FALSE;
    DWORD ioBytes = 0;
    WSAIoctl(sock, SIO_UDP_CONNRESET, &connReset, sizeof(connReset), nullptr, 0, &ioBytes, nullptr, nullptr);

    Log("listening on UDP %u (raw accumulator)", kInputPort);

    char buf[256];
    unsigned int packetCount = 0;
    int lastSentStatusMode = -1;
    long long lastRecvLogMicros = NowMicros();
    long long recvSinceLog = 0;

    while (InterlockedCompareExchange(&g_shutdown, 0, 0) == 0) {
        sockaddr_in from = {};
        int fromLen = sizeof(from);
        const int len = recvfrom(sock, buf, sizeof(buf) - 1, 0,
            reinterpret_cast<sockaddr*>(&from), &fromLen);

        int windowWidth, windowHeight, menuWidth, menuHeight, stableMode;
        double menuCursorX, menuCursorY;
        AcquireSRWLockExclusive(&g_stateLock);
        windowWidth = g_host.windowWidth;
        windowHeight = g_host.windowHeight;
        menuWidth = g_host.menuWidth;
        menuHeight = g_host.menuHeight;
        menuCursorX = g_host.menuCursorX;
        menuCursorY = g_host.menuCursorY;
        stableMode = StableModeLocked();
        ReleaseSRWLockExclusive(&g_stateLock);

        if (len <= 0) {
            const char* status = (stableMode == kCursorModeDisabled) ? "MODE:GAMEPLAY" : "MODE:MENU";
            if (stableMode != lastSentStatusMode) {
                lastSentStatusMode = stableMode;
                char packet[160] = {};
                sprintf_s(packet, "%s cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                    status, menuCursorX, menuCursorY,
                    windowWidth, windowHeight, menuWidth, menuHeight);
                SendStatusText(packet);
            }
            continue;
        }

        buf[len] = 0;
        RememberStatusAddress(from);
        InterlockedExchange(&g_lastActivityTick, (LONG)GetTickCount());

        if (strcmp(buf, "hello") == 0 || strcmp(buf, "ping") == 0) {
            char ack[160] = {};
            sprintf_s(ack, "javauwp_glfw_mouse:ready mode=%d cursor=%.0f,%.0f cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                stableMode,
                WindowToProtocolX(menuCursorX, windowWidth), WindowToProtocolY(menuCursorY, windowHeight),
                menuCursorX, menuCursorY,
                windowWidth, windowHeight, menuWidth, menuHeight);
            sendto(sock, ack, (int)strlen(ack), 0, reinterpret_cast<sockaddr*>(&from), fromLen);
            const char* status = (stableMode == kCursorModeDisabled) ? "MODE:GAMEPLAY" : "MODE:MENU";
            lastSentStatusMode = stableMode;
            char packet[160] = {};
            sprintf_s(packet, "%s cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                status, menuCursorX, menuCursorY, windowWidth, windowHeight, menuWidth, menuHeight);
            SendStatusText(packet);
            Log("handshake replied");
            continue;
        }

        double dx = 0.0, dy = 0.0, wheelY = 0.0;
        int lb = -1, rb = -1, mb = -1, x1 = -1, x2 = -1;
        bool absolutePacket = false;
        bool absoluteWindowPacket = false;
        int fields = 0;
        if (strncmp(buf, "ABSW:", 5) == 0) {
            absolutePacket = true;
            absoluteWindowPacket = true;
            fields = sscanf_s(buf + 5, "%lf,%lf,%d,%d,%d,%lf,%d,%d", &dx, &dy, &lb, &rb, &mb, &wheelY, &x1, &x2);
        } else if (strncmp(buf, "ABS:", 4) == 0) {
            absolutePacket = true;
            fields = sscanf_s(buf + 4, "%lf,%lf,%d,%d,%d,%lf,%d,%d", &dx, &dy, &lb, &rb, &mb, &wheelY, &x1, &x2);
        } else {
            fields = sscanf_s(buf, "%lf,%lf,%d,%d,%d,%lf,%d,%d", &dx, &dy, &lb, &rb, &mb, &wheelY, &x1, &x2);
        }
        if (fields != 8) {
            const char bad[] = "javauwp_glfw_mouse:bad_packet";
            sendto(sock, bad, (int)sizeof(bad) - 1, 0, reinterpret_cast<sockaddr*>(&from), fromLen);
            continue;
        }

        const long long tMicros = NowMicros();
        AcquireSRWLockExclusive(&g_mailboxLock);
        if (absolutePacket) {
            g_mailbox.submitAbsolute(tMicros, dx, dy, absoluteWindowPacket, wheelY);
        } else {
            g_mailbox.submitRelative(tMicros, dx, dy, wheelY);
        }
        g_mailbox.submitButtonValue(1, lb);
        g_mailbox.submitButtonValue(2, rb);
        g_mailbox.submitButtonValue(4, mb);
        g_mailbox.submitButtonValue(8, x1);
        g_mailbox.submitButtonValue(16, x2);
        const long long recvTotal = g_mailbox.receivedCount();
        const int depth = g_mailbox.depth();
        ReleaseSRWLockExclusive(&g_mailboxLock);

        ++recvSinceLog;
        if (g_diag) {
            const long long sinceLog = tMicros - lastRecvLogMicros;
            if (sinceLog >= 1000000) {
                const double pps = (double)recvSinceLog * 1000000.0 / (double)sinceLog;
                DiagLog("recv pps=%.0f total=%lld pending=%d", pps, recvTotal, depth);
                lastRecvLogMicros = tMicros;
                recvSinceLog = 0;
            }
        }

        const char* status = (stableMode == kCursorModeDisabled) ? "MODE:GAMEPLAY" : "MODE:MENU";
        if (stableMode != lastSentStatusMode) {
            lastSentStatusMode = stableMode;
            char packet[160] = {};
            sprintf_s(packet, "%s cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                status, menuCursorX, menuCursorY, windowWidth, windowHeight, menuWidth, menuHeight);
            SendStatusText(packet);
        }

        ++packetCount;
        if (packetCount == 1 || (packetCount % 120) == 0) {
            char ack[160] = {};
            sprintf_s(ack, "javauwp_glfw_mouse:receiving mode=%d cursor=%.0f,%.0f cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                stableMode,
                WindowToProtocolX(menuCursorX, windowWidth), WindowToProtocolY(menuCursorY, windowHeight),
                menuCursorX, menuCursorY,
                windowWidth, windowHeight, menuWidth, menuHeight);
            sendto(sock, ack, (int)strlen(ack), 0, reinterpret_cast<sockaddr*>(&from), fromLen);
        }
    }

    closesocket(sock);
    Log("receive thread stopped");
    return 0;
}

const char* GapBucket(double gapMs) {
    if (gapMs > 500.0) return ">500ms";
    if (gapMs > 100.0) return ">100ms";
    if (gapMs > 50.0) return ">50ms";
    if (gapMs > 33.0) return ">33ms";
    if (gapMs > 16.0) return ">16ms";
    return "<=16ms";
}

}

extern "C" {

MOUSE_SUPPORT_API void MouseSupport_Init(void) {
    if (InterlockedExchange(&g_started, 1) != 0) return;

    WSADATA wsa = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Log("WSAStartup failed");
        InterlockedExchange(&g_started, 0);
        return;
    }

    LARGE_INTEGER freq = {};
    QueryPerformanceFrequency(&freq);
    g_qpcTicksPerMicro = freq.QuadPart > 0 ? (double)freq.QuadPart / 1000000.0 : 1.0;
    LARGE_INTEGER start = {};
    QueryPerformanceCounter(&start);
    g_qpcStart = start.QuadPart;

    char diagBuf[16] = {};
    const DWORD dn = GetEnvironmentVariableA("BANDIT_MOUSE_DIAG", diagBuf, sizeof(diagBuf));
    g_diag = (dn > 0 && diagBuf[0] != '0');

    AcquireSRWLockExclusive(&g_mailboxLock);
    g_mailbox.reset();
    ReleaseSRWLockExclusive(&g_mailboxLock);
    g_lastConsumeMicros = NowMicros();

    InterlockedExchange(&g_shutdown, 0);
    g_receiveThread = CreateThread(nullptr, 0, ReceiveThreadProc, nullptr, 0, nullptr);
    if (g_receiveThread) {
        SetThreadPriority(g_receiveThread, THREAD_PRIORITY_ABOVE_NORMAL);
    }
    Log("initialized (raw passthrough, diag %d)", g_diag ? 1 : 0);
}

MOUSE_SUPPORT_API void MouseSupport_Shutdown(void) {
    if (InterlockedCompareExchange(&g_started, 0, 0) == 0) return;
    InterlockedExchange(&g_shutdown, 1);
    if (g_receiveThread) {
        WaitForSingleObject(g_receiveThread, 300);
        CloseHandle(g_receiveThread);
        g_receiveThread = nullptr;
    }
    if (g_statusSocket != INVALID_SOCKET) {
        closesocket(g_statusSocket);
        g_statusSocket = INVALID_SOCKET;
    }
    if (g_overlaySocket != INVALID_SOCKET) {
        closesocket(g_overlaySocket);
        g_overlaySocket = INVALID_SOCKET;
    }
    if (g_diagFile) {
        fclose(g_diagFile);
        g_diagFile = nullptr;
    }
    InterlockedExchange(&g_started, 0);
    Log("shutdown complete");
}

MOUSE_SUPPORT_API int MouseSupport_IsRunning(void) {
    return InterlockedCompareExchange(&g_started, 0, 0) != 0 ? 1 : 0;
}

MOUSE_SUPPORT_API int MouseSupport_PollFrame(MouseSupportFrame* out) {
    if (!out) return 0;

    const long long now = NowMicros();
    mousesupport::ConsumeResult r;
    AcquireSRWLockExclusive(&g_mailboxLock);
    g_mailbox.consume(now, r);
    ReleaseSRWLockExclusive(&g_mailboxLock);

    out->dx = r.dx;
    out->dy = r.dy;
    out->wheel = r.wheel;
    out->hasAbsolute = r.hasAbsolute ? 1 : 0;
    out->absoluteWindow = r.absoluteWindow ? 1 : 0;
    out->absX = r.absX;
    out->absY = r.absY;
    int buttonCount = r.buttonCount;
    if (buttonCount > MOUSE_SUPPORT_MAX_BUTTONS) buttonCount = MOUSE_SUPPORT_MAX_BUTTONS;
    out->buttonCount = buttonCount;
    for (int i = 0; i < buttonCount; ++i) {
        out->buttons[i].button = r.buttons[i].button;
        out->buttons[i].action = r.buttons[i].action;
    }

    if (g_diag) {
        const double gapMs = (double)(now - g_lastConsumeMicros) / 1000.0;
        if (gapMs > 100.0) {
            DiagLog("STALL-RESUME gap=%.1fms oldestPending=%.1fms emitted=(%.3f,%.3f) samples=%d bucket=%s",
                gapMs, r.appliedAgeMicros / 1000.0, r.dx, r.dy, r.sampleCount, GapBucket(gapMs));
        }
    }
    g_lastConsumeMicros = now;
    return 1;
}

MOUSE_SUPPORT_API void MouseSupport_SetHostState(const MouseSupportHostState* state) {
    if (!state) return;
    AcquireSRWLockExclusive(&g_stateLock);
    if (state->cursorMode != g_host.cursorMode) {
        g_pendingMode = state->cursorMode;
        g_modeChangeTime = GetTickCount64();
    }
    g_host = *state;
    ReleaseSRWLockExclusive(&g_stateLock);
}

MOUSE_SUPPORT_API void MouseSupport_SendCursorSync(double x, double y) {
    char packet[64] = {};
    sprintf_s(packet, "SYNC:%.0f,%.0f", x, y);
    SendStatusText(packet);
}

MOUSE_SUPPORT_API void MouseSupport_SendWindowCursorSync(double x, double y) {
    char packet[64] = {};
    sprintf_s(packet, "SYNCW:%.0f,%.0f", x, y);
    SendStatusText(packet);
}

MOUSE_SUPPORT_API void MouseSupport_UpdateOverlay(double menuCursorX, double menuCursorY, int visible) {
    if (g_overlaySocket == INVALID_SOCKET) {
        g_overlaySocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_overlaySocket == INVALID_SOCKET) return;
    }
    int windowWidth, windowHeight;
    AcquireSRWLockShared(&g_stateLock);
    windowWidth = g_host.windowWidth;
    windowHeight = g_host.windowHeight;
    ReleaseSRWLockShared(&g_stateLock);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kOverlayPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    char packet[64] = {};
    sprintf_s(packet, "CURSOR:%.0f,%.0f,%d",
        WindowToProtocolX(menuCursorX, windowWidth), WindowToProtocolY(menuCursorY, windowHeight), visible);
    sendto(g_overlaySocket, packet, (int)strlen(packet), 0,
        reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
}

MOUSE_SUPPORT_API unsigned int MouseSupport_LastActivityTickMs(void) {
    return (unsigned int)InterlockedCompareExchange(&g_lastActivityTick, 0, 0);
}

MOUSE_SUPPORT_API double MouseSupport_SmoothingMs(void) {
    return 0.0;
}

}
