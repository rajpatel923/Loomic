param(
    [string]$Preset = "debug",
    [string]$Config = "config/server.json",
    [switch]$SkipBuild,
    [switch]$BootstrapVcpkg
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-NormalizedPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (Test-Path $Path) {
        return [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Path).Path)
    }

    return [System.IO.Path]::GetFullPath($Path)
}

function Resolve-Tool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [string[]]$FallbackPaths = @()
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    foreach ($path in $FallbackPaths) {
        if (Test-Path $path) {
            return $path
        }
    }

    throw "Required tool '$Name' was not found. Install it or run through dev.cmd so Visual Studio Build Tools can set up the environment."
}

function Invoke-Step {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Label,

        [Parameter(Mandatory = $true)]
        [scriptblock]$Action
    )

    Write-Host "==> $Label"
    & $Action
}

function New-VcpkgInstallation {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [Parameter(Mandatory = $true)]
        [string]$Source
    )

    if (-not (Test-Path $Root)) {
        return $null
    }

    $normalizedRoot = Get-NormalizedPath $Root
    $toolchainFile = Join-Path $normalizedRoot "scripts\buildsystems\vcpkg.cmake"

    if (-not (Test-Path $toolchainFile)) {
        return $null
    }

    $exePath = Join-Path $normalizedRoot "vcpkg.exe"
    $bootstrapScript = Join-Path $normalizedRoot "bootstrap-vcpkg.bat"

    return [pscustomobject]@{
        Root            = $normalizedRoot
        ToolchainFile   = Get-NormalizedPath $toolchainFile
        ExePath         = $exePath
        BootstrapScript = $bootstrapScript
        HasExecutable   = Test-Path $exePath
        Source          = $Source
    }
}

function Resolve-VcpkgInstallation {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BundledRoot,

        [switch]$ForceBundledBootstrap
    )

    $bundledInstallation = New-VcpkgInstallation -Root $BundledRoot -Source "bundled submodule"
    if ($ForceBundledBootstrap) {
        if (-not $bundledInstallation) {
            throw "Bundled vcpkg checkout was not found at $BundledRoot"
        }

        return $bundledInstallation
    }

    if ($env:VCPKG_ROOT) {
        $envInstallation = New-VcpkgInstallation -Root $env:VCPKG_ROOT -Source "VCPKG_ROOT"
        if ($envInstallation -and $envInstallation.HasExecutable) {
            return $envInstallation
        }
    }

    $pathCommand = Get-Command "vcpkg" -ErrorAction SilentlyContinue
    if ($pathCommand) {
        $pathInstallation = New-VcpkgInstallation -Root (Split-Path -Parent $pathCommand.Source) -Source "PATH"
        if ($pathInstallation -and $pathInstallation.HasExecutable) {
            return $pathInstallation
        }
    }

    if ($bundledInstallation) {
        return $bundledInstallation
    }

    throw "Unable to locate a usable vcpkg installation. Install vcpkg and set VCPKG_ROOT, add it to PATH, or initialize the bundled submodule at $BundledRoot."
}

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CachePath,

        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    if (-not (Test-Path $CachePath)) {
        return $null
    }

    $pattern = "^{0}:[^=]+=(.*)$" -f [regex]::Escape($Name)
    foreach ($line in Get-Content -Path $CachePath) {
        if ($line -match $pattern) {
            return $Matches[1]
        }
    }

    return $null
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

$configPath = if ([System.IO.Path]::IsPathRooted($Config)) {
    $Config
} else {
    Join-Path $scriptDir $Config
}

$bundledVcpkgRoot = Join-Path $scriptDir "vcpkg"
$vcpkg = Resolve-VcpkgInstallation -BundledRoot $bundledVcpkgRoot -ForceBundledBootstrap:$BootstrapVcpkg
$exePath = Join-Path $scriptDir "build\$Preset\bin\LoomicServer.exe"
$buildDir = Join-Path $scriptDir "build\$Preset"
$cmakeCachePath = Join-Path $buildDir "CMakeCache.txt"
$cmakeExe = Resolve-Tool "cmake" @(
    "C:\Program Files\CMake\bin\cmake.exe",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)

Write-Host "==> Using vcpkg from $($vcpkg.Source): $($vcpkg.Root)"
$env:VCPKG_ROOT = $vcpkg.Root

if ($vcpkg.Source -eq "bundled submodule" -and ($BootstrapVcpkg -or -not $vcpkg.HasExecutable)) {
    if (-not (Test-Path $vcpkg.BootstrapScript)) {
        throw "Missing vcpkg bootstrap script at $($vcpkg.BootstrapScript)"
    }

    Invoke-Step "Bootstrapping vcpkg" {
        & $vcpkg.BootstrapScript
        if ($LASTEXITCODE -ne 0) {
            throw "vcpkg bootstrap failed with exit code $LASTEXITCODE"
        }
    }

    $vcpkg = Resolve-VcpkgInstallation -BundledRoot $bundledVcpkgRoot -ForceBundledBootstrap
    $env:VCPKG_ROOT = $vcpkg.Root
}

if (-not $SkipBuild) {
    $configureArgs = @("--preset", $Preset, "-DCMAKE_TOOLCHAIN_FILE=$($vcpkg.ToolchainFile)")
    $cachedToolchain = Get-CMakeCacheValue -CachePath $cmakeCachePath -Name "CMAKE_TOOLCHAIN_FILE"

    if ($cachedToolchain) {
        $normalizedCachedToolchain = Get-NormalizedPath $cachedToolchain
        if ($normalizedCachedToolchain -ne $vcpkg.ToolchainFile) {
            $configureArgs += "--fresh"
        }
    }

    Invoke-Step "Configuring CMake preset '$Preset'" {
        & $cmakeExe @configureArgs
        if ($LASTEXITCODE -ne 0) {
            throw "cmake configure failed with exit code $LASTEXITCODE"
        }
    }

    Invoke-Step "Building CMake preset '$Preset'" {
        & $cmakeExe --build --preset $Preset
        if ($LASTEXITCODE -ne 0) {
            throw "cmake build failed with exit code $LASTEXITCODE"
        }
    }
}

if (-not (Test-Path $configPath)) {
    throw "Missing config file: $configPath"
}

if (-not (Test-Path $exePath)) {
    throw "Missing backend executable: $exePath"
}

New-Item -ItemType Directory -Force -Path (Join-Path $scriptDir "logs") | Out-Null

Invoke-Step "Starting Loomic backend" {
    & $exePath --config $configPath
}
