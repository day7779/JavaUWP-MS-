$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "config.ps1")

function Resolve-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Get-JavaHomeMajorVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$JavaHome
    )

    $javaExe = Join-Path $JavaHome "bin\java.exe"
    $javacExe = Join-Path $JavaHome "bin\javac.exe"
    if (-not (Test-Path $javaExe) -or -not (Test-Path $javacExe)) {
        return $null
    }

    # java -version writes to stderr, and $ErrorActionPreference = 'Stop' in
    # the parent scope turns any captured ErrorRecord into a terminating throw.
    # PowerShell 7's $PSNativeCommandUseErrorActionPreference doesn't exist on
    # 5.1, so relax the error pref itself for just this native call.
    $versionOutput = $null
    try {
        $prevPref = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $versionOutput = (& $javaExe -version 2>&1 | Select-Object -First 1).ToString()
    } catch {
        return $null
    } finally {
        $ErrorActionPreference = $prevPref
    }
    if ($versionOutput -match '"(?<major>\d+)(?:\.(?<minor>\d+))?') {
        $major = [int]$Matches.major
        if ($major -eq 1 -and $Matches.minor) {
            return [int]$Matches.minor
        }
        return $major
    }

    return $null
}

function Test-JavaHomeMinimumVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$JavaHome,

        [Parameter(Mandatory = $true)]
        [int]$MajorVersion
    )

    $major = Get-JavaHomeMajorVersion -JavaHome $JavaHome
    if ($null -eq $major) {
        return $false
    }
    return ($major -ge $MajorVersion)
}

function Test-JavaHomeExactVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$JavaHome,

        [Parameter(Mandatory = $true)]
        [int]$MajorVersion
    )

    $major = Get-JavaHomeMajorVersion -JavaHome $JavaHome
    if ($null -eq $major) {
        return $false
    }
    return ($major -eq $MajorVersion)
}

function Get-JavaHomeCandidates {
    param(
        [Parameter(Mandatory = $true)]
        [int]$MajorVersion
    )

    $candidates = @()
    foreach ($envName in @("JAVA${MajorVersion}_HOME", "JDK${MajorVersion}_HOME", "JAVA_HOME_${MajorVersion}_X64")) {
        $value = [Environment]::GetEnvironmentVariable($envName)
        if ($value) {
            $candidates += Get-Item $value -ErrorAction SilentlyContinue
        }
    }

    $directRoots = @(
        "C:\ms-jdk$MajorVersion",
        "C:\Program Files\Java",
        "C:\Program Files\Eclipse Adoptium",
        "C:\Program Files\Amazon Corretto",
        "C:\Program Files\Microsoft",
        "C:\"
    )

    foreach ($root in $directRoots | Select-Object -Unique) {
        if (-not (Test-Path $root)) { continue }

        if (Test-Path (Join-Path $root "bin\javac.exe")) {
            $candidates += Get-Item $root
            continue
        }

        $candidates += Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Where-Object {
                $_.Name -like "graalvm-community-openjdk-*" -or
                $_.Name -like "jdk-$MajorVersion*" -or
                $_.Name -like "jdk$MajorVersion*" -or
                $_.Name -like "msopenjdk-$MajorVersion*" -or
                $_.Name -like "microsoft-jdk-$MajorVersion*"
            }
    }

    return @($candidates | Where-Object { $_ } | Select-Object -Unique)
}

function Resolve-JavaHomeExact {
    param(
        [Parameter(Mandatory = $true)]
        [int]$MajorVersion
    )

    $candidates = @()
    if ($env:JAVA_HOME) {
        $candidates += Get-Item $env:JAVA_HOME -ErrorAction SilentlyContinue
    }
    $candidates += Get-JavaHomeCandidates -MajorVersion $MajorVersion

    $match = $candidates |
        Where-Object { Test-JavaHomeExactVersion -JavaHome $_.FullName -MajorVersion $MajorVersion } |
        Select-Object -First 1

    if ($match) {
        return $match.FullName
    }

    throw "No exact Java $MajorVersion installation found. Set JAVA${MajorVersion}_HOME or JDK${MajorVersion}_HOME to a JDK $MajorVersion install."
}

function Resolve-JavaHome {
    param(
        [int]$MajorVersion = $ProjectConfig.JavaRelease
    )

    if ($env:JAVA_HOME) {
        if (Test-JavaHomeMinimumVersion -JavaHome $env:JAVA_HOME -MajorVersion $MajorVersion) {
            return $env:JAVA_HOME
        }

        Write-Warning "JAVA_HOME is set but is older than JDK ${MajorVersion}: $env:JAVA_HOME"
    }

    $match = Get-JavaHomeCandidates -MajorVersion $MajorVersion |
        Where-Object { Test-JavaHomeMinimumVersion -JavaHome $_.FullName -MajorVersion $MajorVersion } |
        Select-Object -First 1

    if ($match) {
        return $match.FullName
    }

    throw "No suitable Java installation found. Set JAVA_HOME to a JDK $MajorVersion or newer install."
}

$script:MinecraftJavaMajorCache = @{}

function Get-MinecraftJavaMajorVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$MinecraftVersion
    )

    if ($script:MinecraftJavaMajorCache.ContainsKey($MinecraftVersion)) {
        return $script:MinecraftJavaMajorCache[$MinecraftVersion]
    }

    $major = [int]$ProjectConfig.JavaRelease
    try {
        $manifest = Invoke-RestMethod -UseBasicParsing -TimeoutSec 60 `
            -Uri "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
        $entry = $manifest.versions | Where-Object { $_.id -eq $MinecraftVersion } | Select-Object -First 1
        if ($entry) {
            $versionJson = Invoke-RestMethod -UseBasicParsing -TimeoutSec 60 -Uri $entry.url
            if ($versionJson.javaVersion -and $versionJson.javaVersion.majorVersion) {
                $major = [int]$versionJson.javaVersion.majorVersion
            }
        }
    } catch {
        Write-Warning "Could not read the required Java version for Minecraft ${MinecraftVersion}: $($_.Exception.Message)"
    }

    $script:MinecraftJavaMajorCache[$MinecraftVersion] = $major
    return $major
}

function Resolve-JavaHomeForMinecraft {
    param(
        [Parameter(Mandatory = $true)]
        [string]$MinecraftVersion
    )

    # javac and the remap launch both read the client jar, so the JDK has to be new enough
    # to load its class files. 26.2 is class file 69, which JDK 21 refuses.
    $required = [Math]::Max(
        (Get-MinecraftJavaMajorVersion -MinecraftVersion $MinecraftVersion),
        [int]$ProjectConfig.JavaRelease)
    return Resolve-JavaHome -MajorVersion $required
}

function Resolve-SpongeMixinJar {
    param(
        [Parameter(Mandatory = $true)]
        [string]$GameDir,

        [string]$MinecraftVersion,

        [string]$LoaderVersion
    )

    if ($MinecraftVersion -and $LoaderVersion) {
        $profilePath = Join-Path $GameDir "versions\fabric-loader-$LoaderVersion-$MinecraftVersion\fabric-loader-$LoaderVersion-$MinecraftVersion.json"
        if (Test-Path $profilePath) {
            $profileJson = Get-Content -Raw -Path $profilePath | ConvertFrom-Json
            foreach ($library in $profileJson.libraries) {
                $name = [string]$library.name
                if ($name -notlike "net.fabricmc:sponge-mixin:*") { continue }
                $mixinVersion = $name.Split(":")[2]
                $candidate = Join-Path $GameDir "libraries\net\fabricmc\sponge-mixin\$mixinVersion\sponge-mixin-$mixinVersion.jar"
                if (Test-Path $candidate) { return $candidate }
            }
        }
    }

    $configured = Join-Path $GameDir "libraries\net\fabricmc\sponge-mixin\$($ProjectConfig.MixinVersion)\sponge-mixin-$($ProjectConfig.MixinVersion).jar"
    if (Test-Path $configured) { return $configured }

    $newest = Get-ChildItem -LiteralPath (Join-Path $GameDir "libraries\net\fabricmc\sponge-mixin") `
        -Recurse -Filter "sponge-mixin-*.jar" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if ($newest) { return $newest.FullName }

    throw "sponge-mixin jar not found under $GameDir. Run scripts\prepare-ci-cache.ps1 for this target first."
}

function Test-FabricTargetHasIntermediary {
    param(
        [Parameter(Mandatory = $true)][string]$GameDir,
        [Parameter(Mandatory = $true)][string]$MinecraftVersion,
        [Parameter(Mandatory = $true)][string]$LoaderVersion
    )

    $profilePath = Join-Path $GameDir "versions\fabric-loader-$LoaderVersion-$MinecraftVersion\fabric-loader-$LoaderVersion-$MinecraftVersion.json"
    if (-not (Test-Path $profilePath)) { return $true }

    $profileJson = Get-Content -Raw -Path $profilePath | ConvertFrom-Json
    foreach ($library in $profileJson.libraries) {
        if ([string]$library.name -like "net.fabricmc:intermediary:*") { return $true }
    }

    return $false
}

function Resolve-FabricClientJar {
    param(
        [Parameter(Mandatory = $true)][string]$GameDir,
        [Parameter(Mandatory = $true)][string]$MinecraftVersion,
        [Parameter(Mandatory = $true)][string]$LoaderVersion
    )

    # 26.x ships unobfuscated, so Fabric has no intermediary for it and never writes a
    # remapped jar. Mods for those targets compile straight against the client jar.
    $remapped = Join-Path $GameDir ".fabric\remappedJars\minecraft-$MinecraftVersion-$LoaderVersion\client-intermediary.jar"
    if (Test-Path $remapped) { return $remapped }

    return (Join-Path $GameDir "versions\$MinecraftVersion\$MinecraftVersion.jar")
}

function Resolve-Python {
    if ($env:PYTHON) {
        if (Test-Path $env:PYTHON) {
            return (Resolve-Path $env:PYTHON).Path
        }

        throw "PYTHON is set but does not point to a valid executable: $env:PYTHON"
    }

    $pythonCandidates = @(
        "C:\Users\Dan\AppData\Local\Programs\Python\Python314\python.exe",
        "C:\Program Files\Python314\python.exe"
    )

    foreach ($candidate in $pythonCandidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    $pyLauncher = Get-Command py -ErrorAction SilentlyContinue
    if ($pyLauncher) {
        return $pyLauncher.Source
    }

    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        return $python.Source
    }

    throw "No suitable Python installation found. Set PYTHON to a Python 3 install with Pillow or install Python 3."
}

function Resolve-VSTools {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio Build Tools or Visual Studio with C++ tools."
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installPath) {
        throw "Visual Studio with C++ tools not found."
    }

    $msvcRoot = Get-ChildItem (Join-Path $installPath "VC\Tools\MSVC") -Directory |
        Sort-Object Name -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $msvcRoot) {
        throw "MSVC tools directory not found."
    }

    $clExe = Join-Path $msvcRoot "bin\Hostx64\x64\cl.exe"
    if (-not (Test-Path $clExe)) {
        throw "cl.exe not found at $clExe"
    }

    return @{
        MsvcRoot = $msvcRoot
        ClExe    = $clExe
    }
}

function Resolve-WindowsSdk {
    $sdkRoot = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots").KitsRoot10
    if (-not $sdkRoot) {
        throw "Windows 10/11 SDK not found."
    }

    $sdkVer = Get-ChildItem (Join-Path $sdkRoot "Include") -Directory |
        Sort-Object Name |
        Select-Object -Last 1 -ExpandProperty Name
    if (-not $sdkVer) {
        throw "Windows SDK include directory not found under $sdkRoot."
    }

    return @{
        Root    = $sdkRoot
        Version = $sdkVer
    }
}

function Get-MesaRuntimeDllNames {
    return @(
        "opengl32.dll",
        "libgallium_wgl.dll",
        "spirv_to_dxil.dll",
        "vulkan_dzn.dll",
        "dxil.dll",
        "z-1.dll"
    )
}

function Test-MesaRuntimeDir {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path $Path)) {
        return $false
    }

    $required = @("opengl32.dll", "libgallium_wgl.dll", "dxil.dll", "spirv_to_dxil.dll", "z-1.dll")
    foreach ($dll in $required) {
        if (-not (Test-Path (Join-Path $Path $dll))) {
            return $false
        }
    }

    return $true
}

function Resolve-MesaRuntimeDir {
    param(
        [string]$MesaRuntimeDir
    )

    $candidates = @()

    if ($MesaRuntimeDir) {
        $candidates += $MesaRuntimeDir
    }
    if ($env:MESA_UWP_DIR) {
        $candidates += $env:MESA_UWP_DIR
    }

    $localMesaDir = Get-ConfigPath "MesaRuntimeDir"
    if (Test-Path $localMesaDir) {
        $candidates += $localMesaDir
    }

    $cachedMesaDir = Join-Path (Resolve-RepoRoot) "staging\cache\mesa-runtime"
    if (Test-Path $cachedMesaDir) {
        $candidates += $cachedMesaDir
    }

    # Backward-compatible convenience for users who source Mesa DLLs from
    # RetroArch UWP. RetroArch is not required by the project.
    if ($env:RETROARCH_UWP_DIR) {
        $candidates += $env:RETROARCH_UWP_DIR
    }

    $searchRoots = @("X:\WindowsApps", "S:\Program Files\WindowsApps")
    foreach ($root in $searchRoots) {
        if (-not (Test-Path $root)) { continue }

        $candidates += Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty FullName
    }

    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        if (Test-MesaRuntimeDir -Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "Mesa UWP runtime DLLs not found. Set MESA_UWP_DIR, pass -MesaRuntimeDir, or place the DLLs in the tracked mesa-runtime folder."
}

function Get-ProjectPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    return (Join-Path (Resolve-RepoRoot) $RelativePath)
}

function Get-ConfigPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    if (-not $ProjectConfig.Contains($Name)) {
        throw "Unknown project path config key: $Name"
    }

    return (Get-ProjectPath $ProjectConfig[$Name])
}
