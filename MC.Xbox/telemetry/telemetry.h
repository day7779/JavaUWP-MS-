#pragma once

#include "crash_fingerprint.h"

#include <string>
#include <vector>

namespace telemetry {

struct LaunchContext {
    std::wstring launcherBuild;
    std::wstring mcVersion;
    std::wstring loader;
    std::wstring loaderVersion;
    std::wstring modsetHash;
};

struct CrashRecord {
    bool found = false;
    std::wstring fingerprint;
    std::wstring exception;
    std::wstring message;
    std::wstring phase;
    std::wstring suspectedMod;
    std::wstring launcherBuild;
    std::wstring mcVersion;
    std::wstring loader;
    std::wstring loaderVersion;
    std::wstring modsetHash;
    std::wstring zip;
    int heapMaxMb = 0;
    int heapAtCrashMb = 0;
    std::vector<std::wstring> frames;
    crashfp::CrashDetail detail;
    int repeatCount = 1;
};

enum class ConsentState {
    Unanswered,
    Always,
    Never
};

ConsentState Consent();
bool SetConsent(ConsentState state);

bool ConsentGranted();

std::wstring ConsentPayloadPreview(const CrashRecord& record);

std::wstring InstallIdText();
bool ResetInstallId();

bool Configured();

bool Enabled();

std::wstring EndpointUrl();

std::wstring LauncherBuild();
std::wstring ComputeModsetHash(const std::wstring& modsDir);

void BeginLaunch(const LaunchContext& context);
void EndLaunch();

void SendLaunch();
void SendFirstFrame();
void SendPlayable();
void SendExit();

void QueueSuspend();

void RecordJavaCrash(const crashfp::JavaCrash& crash);

void ReportSoftCrash(const std::wstring& runtimeRoot);

void PrepareHardCrash(const std::wstring& runtimeRoot);
void ReportHardCrash(const std::wstring& runtimeRoot);

void FlushQueueAsync();

void ClearQueue();

CrashRecord ReadLastCrash();
void ClearLastCrash();

void SendLastCrashOnce(const CrashRecord& record);

}
