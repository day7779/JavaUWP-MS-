param(
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path $PSScriptRoot -Parent) "scripts\common.ps1")

$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$sdkRoot = $sdk.Root
$sdkVer = $sdk.Version

if (-not $OutputDir) {
    $OutputDir = Join-Path (Get-ConfigPath "BuildDir") "audio_relay"
}
$OutputDir = (New-Item -ItemType Directory -Force -Path $OutputDir).FullName
$exePath = Join-Path $OutputDir "audio_relay.exe"
$objPath = Join-Path $OutputDir "audio_relay.obj"

$env:INCLUDE = "$($tools.MsvcRoot)\include;" +
               "${sdkRoot}Include\$sdkVer\ucrt;" +
               "${sdkRoot}Include\$sdkVer\shared;" +
               "${sdkRoot}Include\$sdkVer\um;" +
               "${sdkRoot}Include\$sdkVer\winrt;" +
               "${sdkRoot}Include\$sdkVer\cppwinrt"
$env:LIB = "$($tools.MsvcRoot)\lib\x64;" +
           "${sdkRoot}Lib\$sdkVer\ucrt\x64;" +
           "${sdkRoot}Lib\$sdkVer\um\x64"

Push-Location $PSScriptRoot
Write-Host "Building audio_relay.exe (WASAPI loopback -> UDP)..."
& $tools.ClExe audio_relay.cpp /nologo /std:c++17 /EHsc /O2 /MT /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 /Fo"$objPath" `
    /link /OUT:"$exePath" /MACHINE:X64 /SUBSYSTEM:CONSOLE `
    ws2_32.lib ole32.lib
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "audio_relay build FAILED" }
Pop-Location
Write-Host "audio_relay.exe built OK -> $exePath"
