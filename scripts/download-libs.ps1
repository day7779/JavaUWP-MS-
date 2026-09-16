param(
    [string]$MinecraftVersion
)

$ErrorActionPreference = "Stop"

if ($MinecraftVersion) { $env:MC_VERSION = $MinecraftVersion }
. (Join-Path $PSScriptRoot "common.ps1")

$root = Resolve-RepoRoot
$gameDir = Get-ConfigPath "GameDir"
$version = if ($MinecraftVersion) { $MinecraftVersion } else { $ProjectConfig.MinecraftVersion }

$manifest = Get-MinecraftVersionManifest
$v = $manifest.versions | Where-Object { $_.id -eq $version } | Select-Object -First 1
if (-not $v) {
    throw "Minecraft version $version not found in manifest."
}

$vj = Get-CachedRemoteJson -Uri $v.url

$versionDir = Join-Path $gameDir "versions\$version"
Ensure-Dir $versionDir

$clientUrl = $vj.downloads.client.url
$clientJar = Join-Path $versionDir "$version.jar"
Invoke-WebRequest -UseBasicParsing -Uri $clientUrl -OutFile $clientJar
Write-Host "Client jar done -> $clientJar"

$libs = $vj.libraries | Where-Object { $_.downloads.artifact -ne $null }
foreach ($lib in $libs) {
    $artifact = $lib.downloads.artifact
    $dest = Join-Path $gameDir ("libraries\" + $artifact.path.Replace('/', '\'))
    Ensure-Dir (Split-Path $dest)
    if (-not (Test-Path $dest)) {
        Invoke-WebRequest -UseBasicParsing -Uri $artifact.url -OutFile $dest
        Write-Host "Downloaded: $($artifact.path)"
    }
}

Write-Host "All done"
