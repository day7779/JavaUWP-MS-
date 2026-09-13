# keeps crash parsing tests independent of uwp and appx
param(
    [switch]$KeepExe
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
. (Join-Path $root "scripts\common.ps1")

$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$sdkRoot = $sdk.Root
$sdkVer = $sdk.Version

$outDir = Join-Path $root "staging\build\crash_tests"
Ensure-Dir $outDir
$exe = Join-Path $outDir "crash_tests.exe"

$env:INCLUDE = "$($tools.MsvcRoot)\include;${sdkRoot}Include\$sdkVer\ucrt;${sdkRoot}Include\$sdkVer\shared;${sdkRoot}Include\$sdkVer\um"
$env:LIB = "$($tools.MsvcRoot)\lib\x64;${sdkRoot}Lib\$sdkVer\ucrt\x64;${sdkRoot}Lib\$sdkVer\um\x64"

Push-Location (Join-Path $root "MC.Xbox\telemetry")
try {
    & $tools.ClExe tests\crash_tests.cpp crash_fingerprint.cpp crash_parse.cpp `
        /std:c++17 /EHsc $CommonClFlags /Od /Zi /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 `
        /I. /Fo"$outDir\" /Fd"$outDir\" `
        /link /SUBSYSTEM:CONSOLE /MACHINE:X64 /OUT:"$exe" bcrypt.lib
    if ($LASTEXITCODE -ne 0) { throw "compile failed" }
}
finally {
    Pop-Location
}

Write-Host ""
& $exe
$code = $LASTEXITCODE

if (-not $KeepExe) { Remove-Item $exe -Force -ErrorAction SilentlyContinue }
if ($code -ne 0) { throw "crash tests failed" }
Write-Host "CRASH_TESTS_OK"
