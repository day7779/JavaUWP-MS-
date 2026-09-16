param(
    [string]$BuildDir = "staging\build\relay\windows",
    [string]$OutputDir = "dist\relay",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
& (Join-Path $PSScriptRoot "build-windows.ps1") -BuildDir $BuildDir -Config $Config

$buildPath = Join-Path $repoRoot $BuildDir
$packageRoot = Join-Path $repoRoot "staging\package\relay\windows"
$outputPath = Join-Path $repoRoot $OutputDir
$zipPath = Join-Path $outputPath "BanditRelay-nightly-win-x64.zip"
$shaPath = Join-Path $outputPath "BanditRelay-nightly-win-x64.sha256"

Remove-Item -LiteralPath $packageRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$exe = Get-ChildItem -Path $buildPath -Recurse -Filter "BanditRelay.exe" |
    Where-Object { $_.FullName -match "\\$Config\\" -or $_.DirectoryName -eq $buildPath } |
    Select-Object -First 1

if (-not $exe) {
    throw "BanditRelay.exe was not produced under $buildPath"
}

$dll = Get-ChildItem -Path $buildPath -Recurse -Filter "SDL3.dll" |
    Where-Object { $_.FullName -match "\\$Config\\" -or $_.DirectoryName -eq $buildPath } |
    Select-Object -First 1

if (-not $dll) {
    throw "SDL3.dll was not produced under $buildPath"
}

Copy-Item $exe.FullName (Join-Path $packageRoot "BanditRelay.exe") -Force
Copy-Item $dll.FullName (Join-Path $packageRoot "SDL3.dll") -Force
Copy-Item (Join-Path $repoRoot "tools\relay\README.md") (Join-Path $packageRoot "README.md") -Force

Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $packageRoot "*") -DestinationPath $zipPath -Force

$hash = Get-FileHash $zipPath -Algorithm SHA256
"$($hash.Hash.ToLowerInvariant())  BanditRelay-nightly-win-x64.zip" |
    Set-Content -Path $shaPath -Encoding ascii

Write-Host "Packaged Bandit Relay: $zipPath"
