param(
    [string]$MinecraftVersion = "1.21.1",
    [string]$NeoForgeVersion = "21.1.233",
    [string]$NeoFormVersion = "20240808.144430",
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) "scripts\common.ps1")

$root = Resolve-RepoRoot
$gameDir = Get-ConfigPath "GameDir"
$buildRoot = Join-Path (Get-ConfigPath "BuildDir") "controller_mod\neoforge\$NeoForgeVersion"
$classesDir = Join-Path $buildRoot "classes"
$compileOnlyDir = Join-Path $buildRoot "compile-only"
$srcJava = Join-Path $PSScriptRoot "src\main\java"
$srcResources = Join-Path $PSScriptRoot "src\main\resources"
$compileJava = Join-Path $PSScriptRoot "src\compile\java"
$coreJava = Join-Path $root "controller_mod\core\src\main\java"
$jarName = "banditvault-neoforge-controller-1.0.0.jar"
$jarPath = Join-Path $buildRoot $jarName

if ($MinecraftVersion -ne "1.21.1" -or $NeoForgeVersion -ne "21.1.233") {
    throw "NeoForge controller sources currently support only Minecraft 1.21.1 / NeoForge 21.1.233."
}

$javaHome = Resolve-JavaHome
$javac = Join-Path $javaHome "bin\javac.exe"
$jar = Join-Path $javaHome "bin\jar.exe"
$neoFormCoordinate = "$MinecraftVersion-$NeoFormVersion"
$generatedRoot = Join-Path $root "prebuilt\neoforge\libraries"
$srgClient = Join-Path $generatedRoot "net\minecraft\client\$neoFormCoordinate\client-$neoFormCoordinate-srg.jar"
$patchedClient = Join-Path $generatedRoot "net\neoforged\neoforge\$NeoForgeVersion\neoforge-$NeoForgeVersion-client.jar"

if (-not (Test-Path $srgClient) -or -not (Test-Path $patchedClient)) {
    & (Join-Path $root "scripts\gen-neoforge-artifacts.ps1") `
        -NeoForgeVersion $NeoForgeVersion `
        -McVersion $MinecraftVersion `
        -NeoFormVersion $NeoFormVersion
}
if (-not (Test-Path $srgClient) -or -not (Test-Path $patchedClient)) {
    throw "NeoForge compile-only client artifacts are missing after generation."
}

$dependencyDir = Join-Path (Get-ConfigPath "StagingDir") "cache\neoforge\$NeoForgeVersion"
Ensure-Dir $dependencyDir
$universalJar = Join-Path $dependencyDir "neoforge-$NeoForgeVersion-universal.jar"
if (-not (Test-Path $universalJar)) {
    $universalUrl = "https://maven.neoforged.net/releases/net/neoforged/neoforge/$NeoForgeVersion/neoforge-$NeoForgeVersion-universal.jar"
    Invoke-WebRequest -UseBasicParsing -Uri $universalUrl -OutFile $universalJar -TimeoutSec 180
}

$mixinJar = Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries\net\fabricmc\sponge-mixin") -Recurse -Filter "sponge-mixin-*.jar" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $mixinJar) {
    $mixinJar = Join-Path $dependencyDir "sponge-mixin-0.15.2+mixin.0.8.7.jar"
    if (-not (Test-Path $mixinJar)) {
        Invoke-WebRequest -UseBasicParsing `
            -Uri "https://maven.neoforged.net/releases/net/fabricmc/sponge-mixin/0.15.2+mixin.0.8.7/sponge-mixin-0.15.2+mixin.0.8.7.jar" `
            -OutFile $mixinJar `
            -TimeoutSec 180
    }
}

$lwjglJars = @(Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries\org\lwjgl") -Recurse -Filter "*.jar" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*natives*" } |
    Select-Object -ExpandProperty FullName)
if (-not $lwjglJars) {
    throw "LWJGL compile dependencies are missing; prepare the launcher cache first."
}
$libraryJars = @(Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries") -Recurse -Filter "*.jar" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*natives*" } |
    Select-Object -ExpandProperty FullName)

Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
Ensure-Dir $classesDir, $compileOnlyDir

$compileOnlySources = @(Get-ChildItem $compileJava -Recurse -Filter "*.java")
$mainSources = @(Get-ChildItem $srcJava -Recurse -Filter "*.java")
$mainSources += @(Get-ChildItem $coreJava -Recurse -Filter "*.java")
$classpath = @($patchedClient, $srgClient, $universalJar, $mixinJar) + $lwjglJars + $libraryJars

function Invoke-Javac([string]$Name, [string[]]$Sources, [string]$Destination, [string[]]$Classpath) {
    $argsFile = Join-Path $buildRoot "$Name-args.txt"
    $args = @(
        "--release", "21",
        "-proc:none",
        "-classpath", ($Classpath -join [IO.Path]::PathSeparator),
        "-d", $Destination
    ) + $Sources
    [IO.File]::WriteAllLines($argsFile, $args)
    & $javac "@$argsFile"
    if ($LASTEXITCODE -ne 0) {
        throw "NeoForge controller $Name compilation failed."
    }
}

Invoke-Javac "compile-only" @($compileOnlySources.FullName) $compileOnlyDir $classpath
Invoke-Javac "main" @($mainSources.FullName) $classesDir (@($compileOnlyDir) + $classpath)

& (Join-Path $javaHome "bin\java.exe") -ea -cp $classesDir banditvault.controllercore.GridNavigation
if ($LASTEXITCODE -ne 0) {
    throw "NeoForge controller navigation self-check failed."
}

if (Test-Path (Join-Path $classesDir "net\neoforged")) {
    throw "NeoForge controller jar must not ship compile-only NeoForge API classes."
}
Copy-Item -Recurse "$srcResources\*" $classesDir -Force

$manifest = Join-Path $classesDir "META-INF\MANIFEST.MF"
Push-Location $classesDir
try {
    & $jar cfm $jarPath $manifest .
    if ($LASTEXITCODE -ne 0) {
        throw "NeoForge controller jar creation failed."
    }
} finally {
    Pop-Location
}

$listing = & $jar tf $jarPath
foreach ($required in @(
    "banditvault/neoforgecontroller/NeoForgeControllerMod.class",
    "META-INF/neoforge.mods.toml",
    "banditvault-neoforge-controller.mixins.json",
    "pack.mcmeta"
)) {
    if ($listing -notcontains $required) {
        throw "NeoForge controller jar is missing $required"
    }
}
if ($listing | Where-Object { $_ -like "net/neoforged/*" }) {
    throw "NeoForge controller jar contains compile-only NeoForge classes."
}

if ($OutputDir) {
    Ensure-Dir $OutputDir
    Copy-Item $jarPath (Join-Path $OutputDir $jarName) -Force
}
Write-Host "NeoForge controller mod built ($MinecraftVersion / $NeoForgeVersion) -> $jarPath"
