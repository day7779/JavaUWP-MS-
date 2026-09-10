param(
    [string]$CatalogPath
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$root = Resolve-RepoRoot
if (!$CatalogPath) { $CatalogPath = Join-Path $root "config\versions.tsv" }
$catalogTargets = @(Import-Csv $CatalogPath -Delimiter "`t")
$targets = @(
    $catalogTargets | Where-Object { Test-MinecraftVersionAtLeast -Version $_.minecraftVersion -Minimum "1.21" }
)
$failures = New-Object System.Collections.Generic.List[string]

function Require-File([string]$Path, [string]$Label) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        $failures.Add("$Label missing at $Path")
    }
}

$recommendationsPath = Join-Path $root "config\recommended-mods.json"
$packagedRecommendationsPath = Join-Path (Get-ConfigPath "PackageContentDir") "runtime\recommended-mods.json"
Require-File $recommendationsPath "recommended mods config"
Require-File $packagedRecommendationsPath "packaged recommended mods config"

$recommendations = $null
if (Test-Path -LiteralPath $recommendationsPath -PathType Leaf) {
    try {
        $recommendations = Get-Content -Raw -LiteralPath $recommendationsPath | ConvertFrom-Json
    } catch {
        $failures.Add("recommended mods config is not valid JSON")
    }
}

if ($recommendations) {
    foreach ($target in $catalogTargets) {
        $loader = $target.loader.ToLowerInvariant()
        $version = $target.minecraftVersion
        $loaderEntry = $recommendations.PSObject.Properties[$loader]
        if (!$loaderEntry) {
            $failures.Add("recommended mods config is missing loader $loader")
            continue
        }

        $versionEntry = $loaderEntry.Value.PSObject.Properties[$version]
        if (!$versionEntry) {
            $failures.Add("recommended mods config is missing $version $loader")
            continue
        }

        if ($versionEntry.Value -isnot [System.Array]) {
            $failures.Add("recommended mods entry for $version $loader must be an array")
            continue
        }

        $seen = @{}
        foreach ($slug in @($versionEntry.Value)) {
            if ($slug -isnot [string] -or [string]::IsNullOrWhiteSpace($slug)) {
                $failures.Add("recommended mods entry for $version $loader contains an invalid slug")
                continue
            }
            if ($seen.ContainsKey($slug)) {
                $failures.Add("recommended mods entry for $version $loader contains duplicate slug $slug")
            }
            $seen[$slug] = $true
        }
    }
}

if ((Test-Path -LiteralPath $recommendationsPath -PathType Leaf) -and
    (Test-Path -LiteralPath $packagedRecommendationsPath -PathType Leaf) -and
    (Get-FileHash -LiteralPath $recommendationsPath).Hash -ne (Get-FileHash -LiteralPath $packagedRecommendationsPath).Hash) {
    $failures.Add("packaged recommended mods config does not match config\recommended-mods.json")
}

foreach ($target in $targets) {
    $version = $target.minecraftVersion
    $loader = $target.loader.ToLowerInvariant()
    $loaderVersion = $target.loaderVersion
    $targetId = "$version-$loader-$loaderVersion"
    Require-File (Join-Path (Get-ConfigPath "PackageContentDir") "runtime\manifests\$targetId.tsv") "$targetId manifest"

    switch ($loader) {
        "fabric" {
            Require-File (Join-Path (Get-ConfigPath "BuildDir") "compat_mod\$version-$loaderVersion\banditvault-xbox-compat-1.0.0.jar") "$targetId compat mod"
            Require-File (Join-Path (Get-ConfigPath "BuildDir") "fabric_controller_mod\$version-$loaderVersion\banditvault-fabric-controller-1.0.0.jar") "$targetId controller mod"
        }
        "forge" {
            Require-File (Join-Path (Get-ConfigPath "BuildDir") "controller_mod\forge\$version-$loaderVersion\banditvault-forge-controller-1.0.0.jar") "$targetId controller mod"
        }
        "neoforge" {
            Require-File (Join-Path (Get-ConfigPath "BuildDir") "controller_mod\neoforge\$loaderVersion\banditvault-neoforge-controller-1.0.0.jar") "$targetId controller mod"
        }
    }
}

$fabricCount = @($targets | Where-Object loader -eq "fabric").Count
$forgeCount = @($targets | Where-Object loader -eq "forge").Count
$neoForgeCount = @($targets | Where-Object loader -eq "neoforge").Count
if ($targets.Count -ne 47 -or $fabricCount -ne 16 -or $forgeCount -ne 15 -or $neoForgeCount -ne 16) {
    $failures.Add("catalog counts changed. total=$($targets.Count) fabric=$fabricCount forge=$forgeCount neoforge=$neoForgeCount")
}

$compatSources = @(Get-ChildItem (Join-Path $root "controller_mod") -Recurse -Filter "*ControllerCompat.java")
foreach ($source in $compatSources) {
    $text = Get-Content -Raw $source.FullName
    if ($text -match '\|\|\s*button\(GLFW\.GLFW_GAMEPAD_BUTTON_DPAD_(UP|DOWN)\)') {
        $failures.Add("snap navigation still sends D-pad scroll in $($source.FullName)")
    }
}

$blurApi = Get-Content -Raw (Join-Path $root "controller_mod\fabric\src\variants\1.21.6\banditvault\fabriccontroller\FabricScreenApi.java")
if ($blurApi -notmatch 'renderBackground\([^)]*\)\s*\{\s*\}') {
    $failures.Add("Fabric 1.21.6 background adapter can trigger a second blur")
}

$neoScreens = @(
    Join-Path $root "controller_mod\neoforge\src\variants\1.21.1\banditvault\neoforgecontroller\NeoForgeControllerSettingsScreen.java"
    Join-Path $root "controller_mod\neoforge\src\variants\1.21.9\banditvault\neoforgecontroller\NeoForgeControllerSettingsScreen.java"
    Join-Path $root "controller_mod\neoforge\src\variants\26.1\banditvault\neoforgecontroller\NeoForgeControllerSettingsScreen.java"
)
foreach ($screen in $neoScreens) {
    $text = Get-Content -Raw $screen
    if ($text -notmatch 'handleControllerInput' -or $text -notmatch 'buildControlOptions' -or $text -notmatch 'RADIAL') {
        $failures.Add("full controller settings parity missing from $screen")
    }
}

$neoCompat = Get-Content -Raw (Join-Path $root "controller_mod\neoforge\src\main\java\banditvault\neoforgecontroller\NeoForgeControllerCompat.java")
if ($neoCompat -match 'renderedCursor' -or $neoCompat -notmatch 'renderGameplayGuide') {
    $failures.Add("Forge and NeoForge cursor or guide parity regressed")
}

$forgeLoader = Get-Content -Raw (Join-Path $root "MC.Xbox\launch\loaders\forge.cpp")
if ($forgeLoader -notmatch 'out \+= universalJar' -or $forgeLoader -notmatch 'out \+= patchedClient' -or
    $forgeLoader -match 'survivors\.push_back\((universalJar|patchedClient)\)') {
    $failures.Add("Forge game classpath does not append the universal and patched client jars after Maven deduplication")
}

$neoRadials = @(
    Join-Path $root "controller_mod\neoforge\src\variants\1.21.1\banditvault\neoforgecontroller\NeoForgeControllerRadialScreen.java"
    Join-Path $root "controller_mod\neoforge\src\variants\1.21.9\banditvault\neoforgecontroller\NeoForgeControllerRadialScreen.java"
    Join-Path $root "controller_mod\neoforge\src\variants\26.1\banditvault\neoforgecontroller\NeoForgeControllerRadialScreen.java"
)
foreach ($radial in $neoRadials) {
    $text = Get-Content -Raw $radial
    if ($text -notmatch 'drawTile' -or $text -notmatch 'NeoForgeRadialApi\.icon' -or $text -match 'Button\[\]') {
        $failures.Add("full radial screen parity missing from $radial")
    }
}

foreach ($screen in $neoScreens | Select-Object -First 2) {
    if ((Get-Content -Raw $screen) -notmatch 'beginOverlay') {
        $failures.Add("binding picker overlay isolation missing from $screen")
    }
}

foreach ($mixins in @(
    Join-Path $root "controller_mod\neoforge\src\main\resources\banditvault-neoforge-controller.mixins.json"
    Join-Path $root "controller_mod\forge\src\modern\resources\banditvault-forge-controller.mixins.json"
)) {
    if ((Get-Content -Raw $mixins) -notmatch 'NeoForgeControllerHudMixin') {
        $failures.Add("gameplay controller guide mixin missing from $mixins")
    }
}

if ($failures.Count -gt 0) {
    throw "Modern version validation failed`n$($failures -join "`n")"
}

Write-Host "Modern version validation passed 47 targets. Fabric 16, Forge 15, NeoForge 16"
