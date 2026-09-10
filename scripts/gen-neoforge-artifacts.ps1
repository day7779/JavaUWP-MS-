param(
    [string]$NeoForgeVersion = "21.1.233",
    [string]$McVersion = "1.21.1",
    [string]$NeoFormVersion = "20240808.144430",
    [string]$JavaExe
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot "common.ps1")

if (-not $JavaExe) {
    $JavaExe = Join-Path (Resolve-JavaHome) "bin\java.exe"
}
if (-not (Test-Path $JavaExe)) {
    throw "java.exe not found at $JavaExe"
}

$mcAndNeoForm = "$McVersion-$NeoFormVersion"
$work = Join-Path $env:TEMP "nfgen-$NeoForgeVersion"
$target = Join-Path $work "mc"
$installer = Join-Path $work "neoforge-$NeoForgeVersion-installer.jar"
$url = "https://maven.neoforged.net/releases/net/neoforged/neoforge/$NeoForgeVersion/neoforge-$NeoForgeVersion-installer.jar"

Ensure-Dir $target
'{"profiles":{},"settings":{},"version":3}' | Set-Content (Join-Path $target "launcher_profiles.json") -Encoding ascii

if (-not (Test-Path $installer)) {
    Write-Host "Downloading $url"
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $installer -TimeoutSec 180
}

Write-Host "Running NeoForge installer (downloads vanilla + runs processors)"
& $JavaExe -jar $installer --install-client $target
if ($LASTEXITCODE -ne 0) { throw "installer failed ($LASTEXITCODE)" }

$lib = Join-Path $target "libraries"
$srg = "net\minecraft\client\$mcAndNeoForm\client-$mcAndNeoForm-srg.jar"
$extra = "net\minecraft\client\$mcAndNeoForm\client-$mcAndNeoForm-extra.jar"
$patched = "net\neoforged\neoforge\$NeoForgeVersion\neoforge-$NeoForgeVersion-client.jar"
$newPatched = "net\neoforged\minecraft-client-patched\$NeoForgeVersion\minecraft-client-patched-$NeoForgeVersion.jar"
$dstRoot = Join-Path $root "prebuilt\neoforge\libraries"
$newPatchedPath = Join-Path $lib $newPatched
if (Test-Path $newPatchedPath) {
    foreach ($rel in @($srg, $patched)) {
        $dst = Join-Path $dstRoot $rel
        Ensure-Dir (Split-Path $dst -Parent)
        Copy-Item $newPatchedPath $dst -Force
        Write-Host ("staged {0}  ({1:N0} bytes)" -f $rel, (Get-Item $dst).Length)
    }
} else {
    foreach ($rel in @($srg, $extra, $patched)) {
        $src = Join-Path $lib $rel
        if (-not (Test-Path $src)) { throw "installer did not produce $rel" }
        $dst = Join-Path $dstRoot $rel
        Ensure-Dir (Split-Path $dst -Parent)
        Copy-Item $src $dst -Force
        Write-Host ("staged {0}  ({1:N0} bytes)" -f $rel, (Get-Item $dst).Length)
    }
}

Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
Write-Host "done. prebuilt jars updated under prebuilt\neoforge\libraries"
