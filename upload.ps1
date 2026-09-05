[CmdletBinding()]
param(
    [ValidateSet('Auto', 'Uf2', 'Picotool', 'OpenOcd')]
    [string]$Method = 'Auto',

    [string]$BuildDir = 'build',

    [string]$Firmware,

    [switch]$Build,

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',

    [string]$Serial,

    [string]$ProbeSerial,

    [int]$AdapterSpeedKHz = 5000
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

function Resolve-PicoTool {
    $cmd = Get-Command 'picotool' -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    if (-not $script:PicoHome) {
        return $null
    }

    $version = Get-CMakeSetValue -Name 'picotoolVersion' -Default '2.2.0-a4'
    $root = Join-Path (Join-Path $script:PicoHome 'picotool') $version
    if (-not (Test-Path -LiteralPath $root)) {
        $root = Join-Path $script:PicoHome 'picotool'
    }
    if (-not (Test-Path -LiteralPath $root)) {
        return $null
    }

    $tool = Get-ChildItem -LiteralPath $root -Filter 'picotool.exe' -File -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($tool) {
        return $tool.FullName
    }
    return $null
}

function Resolve-OpenOcd {
    $cmd = Get-Command 'openocd' -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    if (-not $script:PicoHome) {
        return $null
    }
    $root = Join-Path $script:PicoHome 'openocd'
    if (-not (Test-Path -LiteralPath $root)) {
        return $null
    }

    $tool = Get-ChildItem -LiteralPath $root -Filter 'openocd.exe' -File -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($tool) {
        return $tool.FullName
    }
    return $null
}

function Resolve-OpenOcdScripts {
    if (-not $script:PicoHome) {
        return $null
    }
    $root = Join-Path $script:PicoHome 'openocd'
    if (-not (Test-Path -LiteralPath $root)) {
        return $null
    }

    $target = Get-ChildItem -LiteralPath $root -Filter 'rp2350.cfg' -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Name -eq 'target' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($target) {
        return (Split-Path -Parent $target.Directory.FullName)
    }
    return $null
}

function Find-BootVolume {
    try {
        $volumes = @(Get-CimInstance Win32_LogicalDisk -ErrorAction Stop |
            Where-Object { $_.VolumeName -eq 'RP2350' -or $_.VolumeName -eq 'RPI-RP2' })
    } catch {
        return $null
    }

    if ($volumes.Count -gt 1) {
        $names = ($volumes | ForEach-Object { "$($_.DeviceID) ($($_.VolumeName))" }) -join ', '
        throw "More than one Pico BOOTSEL drive is mounted: $names. Disconnect the one you do not want to program."
    }
    if ($volumes.Count -eq 1) {
        return $volumes[0]
    }
    return $null
}

function Upload-Uf2Drive {
    param([Parameter(Mandatory = $true)]$Volume)

    $destination = $Volume.DeviceID + '\'
    Write-Host "Copying firmware to $destination ($($Volume.VolumeName))"
    Copy-Item -LiteralPath $script:Uf2Path -Destination $destination -Force
    Write-Host 'UF2 copied. The Pico will reboot automatically.'
}

function Upload-Picotool {
    param([switch]$AllowFailure)

    $tool = Resolve-PicoTool
    if (-not $tool) {
        if ($AllowFailure) { return $false }
        throw 'picotool was not found. Install it or use BOOTSEL / OpenOCD.'
    }

    $args = @('load', '-f', '-v', '-x', $script:Uf2Path)
    if ($Serial) {
        $args += @('--ser', $Serial)
    }

    Write-Host "Uploading with picotool: $tool"
    & $tool @args
    if ($LASTEXITCODE -eq 0) {
        return $true
    }

    if ($AllowFailure) {
        return $false
    }
    throw "picotool failed with exit code $LASTEXITCODE. Put the Hub in BOOTSEL mode or use -Method OpenOcd with a CMSIS-DAP probe."
}

function Upload-OpenOcd {
    param([switch]$AllowFailure)

    if (-not (Test-Path -LiteralPath $script:ElfPath)) {
        if ($AllowFailure) { return $false }
        throw "OpenOCD requires '$script:ElfPath'. Run .\\build.ps1 first."
    }

    $tool = Resolve-OpenOcd
    $scripts = Resolve-OpenOcdScripts
    if (-not $tool -or -not $scripts) {
        if ($AllowFailure) { return $false }
        throw 'Raspberry Pi OpenOCD or its rp2350 scripts were not found.'
    }

    $args = @('-s', $scripts, '-f', 'interface/cmsis-dap.cfg')
    if ($ProbeSerial) {
        $args += @('-c', "adapter serial $ProbeSerial")
    }
    $args += @(
        '-f', 'target/rp2350.cfg',
        '-c', "adapter speed $AdapterSpeedKHz",
        '-c', ('program "' + $script:ElfPath + '" verify reset exit')
    )

    Write-Host "Uploading over SWD with OpenOCD: $tool"
    & $tool @args
    if ($LASTEXITCODE -eq 0) {
        return $true
    }

    if ($AllowFailure) {
        return $false
    }
    throw "OpenOCD failed with exit code $LASTEXITCODE."
}

$script:PicoHome = $null
if ($env:USERPROFILE) {
    $script:PicoHome = Join-Path $env:USERPROFILE '.pico-sdk'
} elseif ($env:HOME) {
    $script:PicoHome = Join-Path $env:HOME '.pico-sdk'
}

if ($Build) {
    & (Join-Path $ProjectRoot 'build.ps1') -Configuration $Configuration -BuildDir $BuildDir
}

if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildPath = [System.IO.Path]::GetFullPath($BuildDir)
} else {
    $BuildPath = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $BuildDir))
}

if ($Firmware) {
    if ([System.IO.Path]::IsPathRooted($Firmware)) {
        $script:Uf2Path = [System.IO.Path]::GetFullPath($Firmware)
    } else {
        $script:Uf2Path = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $Firmware))
    }
} else {
    $script:Uf2Path = Join-Path $BuildPath 'Hub.uf2'
}
$script:ElfPath = Join-Path $BuildPath 'Hub.elf'

if (-not (Test-Path -LiteralPath $script:Uf2Path)) {
    throw "Firmware '$script:Uf2Path' was not found. Run .\\build.ps1 or use .\\upload.ps1 -Build."
}

switch ($Method) {
    'Uf2' {
        $volume = Find-BootVolume
        if (-not $volume) {
            throw 'No RP2350/RPI-RP2 BOOTSEL drive is mounted. Hold BOOTSEL while connecting/resetting the Pico 2 W, then retry.'
        }
        Upload-Uf2Drive -Volume $volume
        break
    }

    'Picotool' {
        [void](Upload-Picotool)
        break
    }

    'OpenOcd' {
        [void](Upload-OpenOcd)
        break
    }

    default {
        $volume = Find-BootVolume
        if ($volume) {
            Upload-Uf2Drive -Volume $volume
            break
        }

        if (Upload-Picotool -AllowFailure) {
            break
        }

        if (Upload-OpenOcd -AllowFailure) {
            break
        }

        throw @'
No usable upload path was found.

Options:
  1. Put the Pico 2 W in BOOTSEL mode and rerun .\upload.ps1.
  2. Connect a CMSIS-DAP / Raspberry Pi Debug Probe and rerun with -Method OpenOcd.
  3. If you later enable USB stdio in the firmware, picotool -f can reboot a running target into BOOTSEL automatically.
'@
    }
}
