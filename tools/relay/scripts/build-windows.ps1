param(
    [string]$BuildDir = "staging\build\relay\windows",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$sourceDir = Join-Path $repoRoot "tools\relay"
$buildPath = Join-Path $repoRoot $BuildDir

New-Item -ItemType Directory -Force -Path $buildPath | Out-Null

cmake -S $sourceDir -B $buildPath -DCMAKE_BUILD_TYPE=$Config
cmake --build $buildPath --config $Config --parallel

$exe = Get-ChildItem -Path $buildPath -Recurse -Filter "BanditRelay.exe" |
    Where-Object { $_.FullName -match "\\$Config\\" -or $_.DirectoryName -eq $buildPath } |
    Select-Object -First 1

if (-not $exe) {
    throw "BanditRelay.exe was not produced under $buildPath"
}

Write-Host "Bandit Relay built: $($exe.FullName)"

