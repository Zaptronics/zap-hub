[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',

    [string]$BuildDir = 'build',

    [string]$Board = 'pico2_w',

    [string]$WifiSsid = $env:ZAP_HUB_WIFI_SSID,

    [string]$WifiPassword = $env:ZAP_HUB_WIFI_PASSWORD,

    [string]$ZapEEPath = $env:ZAPEE_SOURCE,

    [switch]$ConfigureOnly,

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$CMakeLists = Join-Path $ProjectRoot 'CMakeLists.txt'

function Get-CMakeSetValue {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Default
    )

    $text = Get-Content -LiteralPath $CMakeLists -Raw
    $pattern = '(?m)^\s*set\(' + [regex]::Escape($Name) + '\s+"?([^"\s\)]+)"?\s*\)'
    $match = [regex]::Match($text, $pattern)
    if ($match.Success) {
        return $match.Groups[1].Value
    }
    return $Default
}

function Resolve-Executable {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [string[]]$SearchRoots = @()
    )

    $found = Get-Command $Command -ErrorAction SilentlyContinue
    if ($found) {
        return $found.Source
    }

    foreach ($root in $SearchRoots) {
        if (-not $root -or -not (Test-Path -LiteralPath $root)) {
            continue
        }
        $leaf = $Command
        if ($env:OS -eq 'Windows_NT' -and -not $leaf.EndsWith('.exe')) {
            $leaf += '.exe'
        }
        $candidate = Get-ChildItem -LiteralPath $root -Filter $leaf -File -Recurse -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }

    return $null
}

$SdkVersion = Get-CMakeSetValue -Name 'sdkVersion' -Default '2.2.0'
$ToolchainVersion = Get-CMakeSetValue -Name 'toolchainVersion' -Default '14_2_Rel1'
$PicotoolVersion = Get-CMakeSetValue -Name 'picotoolVersion' -Default '2.2.0-a4'

$PicoHome = $null
if ($env:USERPROFILE) {
    $PicoHome = Join-Path $env:USERPROFILE '.pico-sdk'
} elseif ($env:HOME) {
    $PicoHome = Join-Path $env:HOME '.pico-sdk'
}

$searchCMake = @()
$searchNinja = @()
if ($PicoHome) {
    $searchCMake += Join-Path $PicoHome 'cmake'
    $searchNinja += Join-Path $PicoHome 'ninja'

    if (-not $env:PICO_SDK_PATH) {
        $sdk = Join-Path (Join-Path $PicoHome 'sdk') $SdkVersion
        if (Test-Path -LiteralPath $sdk) {
            $env:PICO_SDK_PATH = $sdk
        }
    }

    if (-not $env:PICO_TOOLCHAIN_PATH) {
        $toolchain = Join-Path (Join-Path $PicoHome 'toolchain') $ToolchainVersion
        if (Test-Path -LiteralPath $toolchain) {
            $env:PICO_TOOLCHAIN_PATH = $toolchain
        }
    }
}

$CMake = Resolve-Executable -Command 'cmake' -SearchRoots $searchCMake
$Ninja = Resolve-Executable -Command 'ninja' -SearchRoots $searchNinja

if (-not $CMake) {
    throw 'CMake was not found. Install it or install the Raspberry Pi Pico VS Code extension toolchain.'
}
if (-not $Ninja) {
    throw 'Ninja was not found. Install it or install the Raspberry Pi Pico VS Code extension toolchain.'
}
if (-not $env:PICO_SDK_PATH -or -not (Test-Path -LiteralPath $env:PICO_SDK_PATH)) {
    throw "Pico SDK $SdkVersion was not found. Expected the VS Code extension install under '$PicoHome' or PICO_SDK_PATH to be set."
}
if (-not $env:PICO_TOOLCHAIN_PATH -or -not (Test-Path -LiteralPath $env:PICO_TOOLCHAIN_PATH)) {
    throw "Pico toolchain $ToolchainVersion was not found. Expected the VS Code extension install under '$PicoHome' or PICO_TOOLCHAIN_PATH to be set."
}

if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildPath = [System.IO.Path]::GetFullPath($BuildDir)
} else {
    $BuildPath = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $BuildDir))
}

# ZapEE normally comes from the exact GitHub tag pinned in CMakeLists.txt.
# A path parameter/environment variable is an explicit local-development
# override; do not auto-detect sibling checkouts, because that could silently
# replace the released dependency with uncommitted local code.
if ($ZapEEPath) {
    $ZapEEPath = [System.IO.Path]::GetFullPath($ZapEEPath)
    if (-not (Test-Path -LiteralPath (Join-Path $ZapEEPath 'CMakeLists.txt')) -or
        -not (Test-Path -LiteralPath (Join-Path $ZapEEPath 'include\zaptronics\modules\ztm_pwm.h'))) {
        throw "'$ZapEEPath' is not a valid ZapEE checkout with ZTM_PWM."
    }
}

if ($Clean -and (Test-Path -LiteralPath $BuildPath)) {
    Write-Host "Removing $BuildPath"
    Remove-Item -LiteralPath $BuildPath -Recurse -Force
}

$configureArgs = @(
    '-S', $ProjectRoot,
    '-B', $BuildPath,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_MAKE_PROGRAM=$Ninja",
    "-DPICO_BOARD=$Board"
)

if ($ZapEEPath) {
    $configureArgs += "-DZAPEE_SOURCE=$ZapEEPath"
}

if ($PicoHome) {
    $picotoolDir = Join-Path (Join-Path (Join-Path $PicoHome 'picotool') $PicotoolVersion) 'picotool'
    if (Test-Path -LiteralPath $picotoolDir) {
        $configureArgs += "-Dpicotool_DIR=$picotoolDir"
    }
}

if ($WifiSsid) {
    $configureArgs += "-DHUB_WIFI_SSID=$WifiSsid"
}
if ($WifiPassword) {
    $configureArgs += "-DHUB_WIFI_PASSWORD=$WifiPassword"
}

Write-Host "Configuring Hub ($Configuration, $Board)"
Write-Host "  SDK      : $env:PICO_SDK_PATH"
Write-Host "  Toolchain: $env:PICO_TOOLCHAIN_PATH"
Write-Host "  CMake    : $CMake"
Write-Host "  Ninja    : $Ninja"
if ($ZapEEPath) {
    Write-Host "  ZapEE    : local $ZapEEPath"
} else {
    Write-Host "  ZapEE    : managed by zap.yml / cmake\zap_deps.cmake"
}

& $CMake @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

if ($ConfigureOnly) {
    Write-Host "Hub configuration complete. Dependencies and IDE metadata are ready; compilation was skipped."
    return
}

Write-Host 'Building Hub'
& $CMake --build $BuildPath --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}

$Uf2 = Join-Path $BuildPath 'Hub.uf2'
$Elf = Join-Path $BuildPath 'Hub.elf'
if (-not (Test-Path -LiteralPath $Uf2)) {
    throw "Build completed but '$Uf2' was not produced."
}

Write-Host ''
Write-Host 'Build complete:'
Write-Host "  UF2: $Uf2"
if (Test-Path -LiteralPath $Elf) {
    Write-Host "  ELF: $Elf"
}
