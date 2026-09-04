param(
    [switch]$Quiet
)

# variant-only sources are hand copied into every version directory, this names the copies
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$root = Resolve-RepoRoot
$variantRoot = Join-Path $root "controller_mod\fabric\src\variants"
if (-not (Test-Path $variantRoot)) {
    throw "Variant root not found: $variantRoot"
}

$files = Get-ChildItem $variantRoot -Recurse -File -Filter *.java
$byContent = $files | Group-Object { (Get-FileHash $_.FullName -Algorithm SHA256).Hash }

$crossVersion = foreach ($group in $byContent) {
    if ($group.Count -le 1) { continue }
    $versions = $group.Group | ForEach-Object {
        ($_.FullName.Substring($variantRoot.Length + 1) -split '\\')[0]
    }
    if (($versions | Sort-Object -Unique).Count -gt 1) { $group }
}

$redundant = 0
foreach ($group in $crossVersion) { $redundant += $group.Count - 1 }

if (-not $Quiet) {
    foreach ($group in $crossVersion | Sort-Object { $_.Group[0].Name }) {
        Write-Host $group.Group[0].Name
        foreach ($file in $group.Group | Sort-Object FullName) {
            Write-Host ("    " + $file.FullName.Substring($variantRoot.Length + 1))
        }
    }
    Write-Host ""
}

Write-Host "$($files.Count) variant sources, $redundant redundant across version directories"

if ($redundant -gt 0) {
    throw "$redundant variant sources are byte-identical to a copy under another version directory."
}
