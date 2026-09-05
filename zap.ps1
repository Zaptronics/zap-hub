# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Command = '',

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Arguments = @()
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ConfigPath = Join-Path $ProjectRoot 'zap.yml'
$CMakePath = Join-Path $ProjectRoot 'CMakeLists.txt'
$GeneratedDir = Join-Path $ProjectRoot 'cmake'
$GeneratedCMakePath = Join-Path $GeneratedDir 'zap_deps.cmake'
$GeneratedZephyrConfPath = Join-Path $GeneratedDir 'zap_zephyr.conf'

function Get-YamlScalar {
    param([string]$Value)
    $v = $Value.Trim()
    if ($v.Length -ge 2) {
        if (($v[0] -eq '"' -and $v[$v.Length - 1] -eq '"') -or
            ($v[0] -eq "'" -and $v[$v.Length - 1] -eq "'")) {
            return $v.Substring(1, $v.Length - 2)
        }
    }
    return $v
}

function ConvertTo-Bool {
    param([object]$Value)
    if ($null -eq $Value) { return $false }
    $text = ([string]$Value).Trim().ToLowerInvariant()
    return $text -in @('1', 'true', 'yes', 'on', 'y')
}

function Read-ZapConfig {
    if (-not (Test-Path -LiteralPath $ConfigPath)) {
        throw "zap.yml was not found. Run '.\\zap new' first."
    }

    $config = [ordered]@{
        schema = '1'
        project = [ordered]@{}
        dependencies = [ordered]@{}
    }

    $section = ''
    $currentDependency = $null
    $currentList = $null
    $lineNumber = 0

    foreach ($rawLine in Get-Content -LiteralPath $ConfigPath) {
        $lineNumber++
        if ($rawLine -match '^\s*$' -or $rawLine -match '^\s*#') {
            continue
        }

        $indent = $rawLine.Length - $rawLine.TrimStart().Length
        if (($indent % 2) -ne 0) {
            throw "zap.yml line $lineNumber uses odd indentation. Use two spaces per level."
        }

        $text = $rawLine.Trim()
        if ($text -eq 'project:') {
            $section = 'project'
            $currentDependency = $null
            $currentList = $null
            continue
        }
        if ($text -eq 'dependencies:') {
            $section = 'dependencies'
            $currentDependency = $null
            $currentList = $null
            continue
        }

        if ($indent -eq 0) {
            $parts = $text.Split(':', 2)
            if ($parts.Count -ne 2) {
                throw "Invalid zap.yml line ${lineNumber}: $text"
            }
            if ($parts[0].Trim() -eq 'schema') {
                $config.schema = Get-YamlScalar $parts[1]
                continue
            }
            throw "Unknown top-level zap.yml key on line ${lineNumber}: $($parts[0].Trim())"
        }

        if ($section -eq 'project' -and $indent -eq 2) {
            $parts = $text.Split(':', 2)
            if ($parts.Count -ne 2) {
                throw "Invalid project setting on zap.yml line $lineNumber."
            }
            $config.project[$parts[0].Trim()] = Get-YamlScalar $parts[1]
            continue
        }

        if ($section -eq 'dependencies') {
            if ($indent -eq 2 -and $text.EndsWith(':')) {
                $name = $text.Substring(0, $text.Length - 1).Trim()
                if (-not $name) { throw "Empty dependency name on line $lineNumber." }
                $dep = [ordered]@{
                    type = 'git'
                    uri = ''
                    version = ''
                    hash = ''
                    override_var = ''
                    zephyr_module = 'false'
                    components = @()
                    zephyr_config = @()
                }
                $config.dependencies[$name] = $dep
                $currentDependency = $name
                $currentList = $null
                continue
            }

            if (-not $currentDependency) {
                throw "Dependency setting on line $lineNumber appears before a dependency name."
            }

            $dep = $config.dependencies[$currentDependency]
            if ($indent -eq 4) {
                $parts = $text.Split(':', 2)
                if ($parts.Count -ne 2) {
                    throw "Invalid dependency setting on zap.yml line $lineNumber."
                }
                $key = $parts[0].Trim()
                $value = Get-YamlScalar $parts[1]
                if ($key -in @('components', 'zephyr_config')) {
                    if ($value) {
                        throw "zap.yml line ${lineNumber}: '$key' must be a YAML list."
                    }
                    $currentList = $key
                    continue
                }
                if (-not $dep.Contains($key)) {
                    throw "Unknown dependency key '$key' on zap.yml line $lineNumber."
                }
                $dep[$key] = $value
                $currentList = $null
                continue
            }

            if ($indent -eq 6 -and $text.StartsWith('- ')) {
                if (-not $currentList) {
                    throw "List item on zap.yml line $lineNumber has no list key."
                }
                $item = Get-YamlScalar $text.Substring(2)
                $dep[$currentList] += $item
                continue
            }
        }

        throw "Unsupported zap.yml structure on line ${lineNumber}: $text"
    }

    if ($config.schema -ne '1') {
        throw "Unsupported zap.yml schema '$($config.schema)'. This zap tool supports schema 1."
    }
    return $config
}

function Get-ProjectValue {
    param($Config, [string]$Name, [string]$Default = '')
    if ($Config.project.Contains($Name) -and $Config.project[$Name]) {
        return [string]$Config.project[$Name]
    }
    return $Default
}

function Escape-CMake {
    param([string]$Value)
    if ($null -eq $Value) { return '' }
    $v = $Value.Replace('\\', '\\\\').Replace('"', '\"').Replace(';', '\;')
    return $v
}

function Normalize-Text {
    param([string]$Text)
    if ($null -eq $Text) { return '' }
    return $Text.Replace("`r`n", "`n").Replace("`r", "`n")
}

function Get-GeneratedCMakeText {
    param($Config)

    $environment = (Get-ProjectValue $Config 'environment' 'generic').ToLowerInvariant()
    $depsDir = Get-ProjectValue $Config 'deps_dir' 'deps'
    $target = Get-ProjectValue $Config 'target' ''

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('# Generated by zap.ps1 from ../zap.yml. DO NOT EDIT BY HAND.')
    $lines.Add('# Experienced CMake users may replace this file with normal FetchContent/add_subdirectory logic.')
    $lines.Add('include(FetchContent)')
    $lines.Add('')
    $lines.Add('set(ZAP_DEPS_DIR "' + (Escape-CMake $depsDir) + '" CACHE PATH "Zap dependency source directory")')
    $lines.Add('if(NOT IS_ABSOLUTE "${ZAP_DEPS_DIR}")')
    $lines.Add('    set(ZAP_DEPS_DIR "${CMAKE_SOURCE_DIR}/${ZAP_DEPS_DIR}")')
    $lines.Add('endif()')
    $lines.Add('file(MAKE_DIRECTORY "${ZAP_DEPS_DIR}")')
    $lines.Add('')

    $linkTargets = @()
    foreach ($name in $Config.dependencies.Keys) {
        $dep = $Config.dependencies[$name]
        $safeName = ($name -replace '[^A-Za-z0-9_]', '_')
        $depType = ([string]$dep.type).ToLowerInvariant()
        $isZephyrModule = ConvertTo-Bool $dep.zephyr_module

        if ($environment -eq 'zephyr' -and $isZephyrModule) {
            $lines.Add("# $name is supplied to Zephyr through ZEPHYR_MODULES by the zap tool.")
            $lines.Add('')
            continue
        }

        $overrideVar = [string]$dep.override_var
        if ($overrideVar) {
            $lines.Add(('set({0} "" CACHE PATH "Optional local source override for {1}")' -f $overrideVar, $name))
            $lines.Add("if($overrideVar)")
            $lines.Add('    FetchContent_Declare(')
            $lines.Add("        $safeName")
            $lines.Add('        SOURCE_DIR "${' + $overrideVar + '}"')
            $lines.Add(('        BINARY_DIR "${{CMAKE_BINARY_DIR}}/_zapdeps/{0}"' -f $safeName))
            $lines.Add('    )')
            $lines.Add('else()')
        }

        $prefix = if ($overrideVar) { '    ' } else { '' }
        $lines.Add($prefix + 'FetchContent_Declare(')
        $lines.Add($prefix + "    $safeName")
        switch ($depType) {
            'git' {
                if (-not $dep.uri -or -not $dep.version) {
                    throw "Dependency '$name' requires uri and version for type git."
                }
                $lines.Add($prefix + '    GIT_REPOSITORY "' + (Escape-CMake ([string]$dep.uri)) + '"')
                $lines.Add($prefix + '    GIT_TAG        "' + (Escape-CMake ([string]$dep.version)) + '"')
                $lines.Add($prefix + '    GIT_SHALLOW    TRUE')
                $lines.Add($prefix + '    GIT_PROGRESS   TRUE')
                $lines.Add($prefix + '    SOURCE_DIR     "${ZAP_DEPS_DIR}/' + (Escape-CMake $safeName) + '"')
                $lines.Add($prefix + '    BINARY_DIR     "${CMAKE_BINARY_DIR}/_zapdeps/' + (Escape-CMake $safeName) + '"')
            }
            'url' {
                if (-not $dep.uri) { throw "Dependency '$name' requires uri for type url." }
                $lines.Add($prefix + '    URL            "' + (Escape-CMake ([string]$dep.uri)) + '"')
                if ($dep.hash) {
                    $lines.Add($prefix + '    URL_HASH       "' + (Escape-CMake ([string]$dep.hash)) + '"')
                }
                $lines.Add($prefix + '    SOURCE_DIR     "${ZAP_DEPS_DIR}/' + (Escape-CMake $safeName) + '"')
                $lines.Add($prefix + '    BINARY_DIR     "${CMAKE_BINARY_DIR}/_zapdeps/' + (Escape-CMake $safeName) + '"')
            }
            'path' {
                if (-not $dep.uri) { throw "Dependency '$name' requires uri for type path." }
                $lines.Add($prefix + '    SOURCE_DIR     "' + (Escape-CMake ([string]$dep.uri)) + '"')
                $lines.Add($prefix + '    BINARY_DIR     "${CMAKE_BINARY_DIR}/_zapdeps/' + (Escape-CMake $safeName) + '"')
            }
            default {
                throw "Dependency '$name' has unsupported type '$depType'. Use git, url, or path."
            }
        }
        $lines.Add($prefix + ')')
        if ($overrideVar) { $lines.Add('endif()') }
        $lines.Add("FetchContent_MakeAvailable($safeName)")
        $lines.Add('')

        foreach ($component in $dep.components) {
            if ($component) { $linkTargets += [string]$component }
        }
    }

    $lines.Add('function(zap_apply_dependencies target_name)')
    $lines.Add('    if(NOT TARGET "${target_name}")')
    $lines.Add('        message(FATAL_ERROR "zap_apply_dependencies: target ''${target_name}'' does not exist")')
    $lines.Add('    endif()')
    if ($linkTargets.Count -gt 0 -and $environment -ne 'zephyr') {
        # Set LINK_LIBRARIES directly instead of calling target_link_libraries().
        # This is intentionally signature-agnostic: existing projects may already
        # use either CMake's plain or keyword target_link_libraries() form.
        foreach ($component in $linkTargets) {
            $lines.Add('    set_property(TARGET "${target_name}" APPEND PROPERTY LINK_LIBRARIES ' + $component + ')')
        }
    } else {
        $lines.Add('    # No direct CMake component links are required for this configuration.')
    }
    $lines.Add('endfunction()')
    $lines.Add('')

    return ($lines -join [Environment]::NewLine) + [Environment]::NewLine
}

function Get-GeneratedZephyrConfText {
    param($Config)
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('# Generated by zap.ps1 from ../zap.yml. DO NOT EDIT BY HAND.')
    foreach ($name in $Config.dependencies.Keys) {
        $dep = $Config.dependencies[$name]
        foreach ($item in $dep.zephyr_config) {
            if ($item) { $lines.Add([string]$item) }
        }
    }
    if ($lines.Count -eq 1) {
        $lines.Add('# No Zephyr Kconfig fragments are requested.')
    }
    return ($lines -join [Environment]::NewLine) + [Environment]::NewLine
}

function Write-GeneratedFiles {
    param($Config)
    if (-not (Test-Path -LiteralPath $GeneratedDir)) {
        New-Item -ItemType Directory -Path $GeneratedDir | Out-Null
    }
    [System.IO.File]::WriteAllText($GeneratedCMakePath, (Get-GeneratedCMakeText $Config), (New-Object System.Text.UTF8Encoding($false)))
    [System.IO.File]::WriteAllText($GeneratedZephyrConfPath, (Get-GeneratedZephyrConfText $Config), (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "Generated $GeneratedCMakePath"
}

function Get-InferredTargetName {
    if (-not (Test-Path -LiteralPath $CMakePath)) { return 'app' }
    $text = Get-Content -LiteralPath $CMakePath -Raw
    $m = [regex]::Match($text, '(?m)^\s*add_executable\s*\(\s*([A-Za-z0-9_.+-]+)')
    if ($m.Success) { return $m.Groups[1].Value }
    $m = [regex]::Match($text, '(?m)^\s*project\s*\(\s*([A-Za-z0-9_.+-]+)')
    if ($m.Success) { return $m.Groups[1].Value }
    return 'app'
}

function Get-InferredEnvironment {
    if (-not (Test-Path -LiteralPath $CMakePath)) { return 'generic' }
    $text = Get-Content -LiteralPath $CMakePath -Raw
    if ($text -match 'pico_sdk_import|pico_sdk_init') { return 'pico-sdk' }
    if ($text -match 'find_package\s*\(\s*Zephyr') { return 'zephyr' }
    return 'generic'
}

function Ensure-CMakeIntegration {
    param($Config)
    if (-not (Test-Path -LiteralPath $CMakePath)) {
        throw 'CMakeLists.txt was not found in the project root.'
    }

    $text = Get-Content -LiteralPath $CMakePath -Raw
    $changed = $false
    $includeBegin = '# ZAP MANAGED DEPENDENCIES BEGIN'
    $includeEnd = '# ZAP MANAGED DEPENDENCIES END'
    $applyBegin = '# ZAP MANAGED TARGETS BEGIN'
    $applyEnd = '# ZAP MANAGED TARGETS END'

    if (-not $text.Contains($includeBegin)) {
        $block = "$includeBegin`r`ninclude(`"`${CMAKE_CURRENT_LIST_DIR}/cmake/zap_deps.cmake`")`r`n$includeEnd`r`n"
        $environment = (Get-ProjectValue $Config 'environment' (Get-InferredEnvironment)).ToLowerInvariant()
        if ($environment -eq 'pico-sdk') {
            # Pico-specific ZapEE targets are created only after pico_sdk_init()
            # has defined the SDK's CMake targets (hardware_i2c, pico_sync, ...).
            $anchor = [regex]::Match($text, '(?m)^\s*pico_sdk_init\s*\(\s*\)\s*$')
            if (-not $anchor.Success) {
                throw 'Could not find pico_sdk_init() in CMakeLists.txt. Add cmake/zap_deps.cmake after pico_sdk_init() manually.'
            }
        } else {
            $anchor = [regex]::Match($text, '(?m)^\s*project\s*\([^\r\n]+\)\s*$')
            if (-not $anchor.Success) {
                throw 'Could not find project(...) in CMakeLists.txt. Add cmake/zap_deps.cmake manually.'
            }
        }
        $insertAt = $anchor.Index + $anchor.Length
        $text = $text.Insert($insertAt, "`r`n`r`n$block")
        $changed = $true
    }

    if (-not $text.Contains($applyBegin)) {
        $target = Get-ProjectValue $Config 'target' (Get-InferredTargetName)
        $block = "`r`n$applyBegin`r`nif(COMMAND zap_apply_dependencies)`r`n    zap_apply_dependencies($target)`r`nendif()`r`n$applyEnd`r`n"
        $text = $text.TrimEnd() + "`r`n" + $block
        $changed = $true
    }

    if ($changed) {
        [System.IO.File]::WriteAllText($CMakePath, $text, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host 'Updated CMakeLists.txt with Zap dependency integration markers.'
    } else {
        Write-Host 'CMakeLists.txt already contains Zap dependency integration markers.'
    }
}

function Write-NewConfig {
    if (Test-Path -LiteralPath $ConfigPath) {
        throw 'zap.yml already exists.'
    }

    $environment = Get-InferredEnvironment
    $answer = Read-Host "Environment [pico-sdk/zephyr/generic] ($environment)"
    if ($answer) { $environment = $answer.Trim().ToLowerInvariant() }
    if ($environment -notin @('pico-sdk', 'zephyr', 'generic')) {
        throw "Unsupported environment '$environment'."
    }

    $target = Get-InferredTargetName
    $answer = Read-Host "CMake application target ($target)"
    if ($answer) { $target = $answer.Trim() }

    $board = ''
    if ($environment -eq 'pico-sdk') {
        $board = 'pico2_w'
        $answer = Read-Host "Pico board ($board)"
        if ($answer) { $board = $answer.Trim() }
    } elseif ($environment -eq 'zephyr') {
        $board = Read-Host 'Zephyr board (for example nucleo_f429zi)'
    }

    $lines = @(
        '# Zap dependency manifest',
        'schema: 1',
        '',
        'project:',
        "  environment: $environment",
        "  target: $target",
        '  build_dir: build',
        '  deps_dir: deps',
        '  adapters_dir: adapters'
    )
    if ($board) { $lines += "  board: $board" }
    $defaultComponent = if ($environment -eq 'pico-sdk') { 'zap::pico_ztm_pwm' } else { 'zap::ztm_pwm' }
    $lines += @(
        '',
        'dependencies:',
        '  zapee:',
        '    type: git',
        '    uri: git@github.com:Zaptronics/zap-ee.git',
        '    version: v0.4.1',
        '    override_var: ZAPEE_SOURCE',
        '    zephyr_module: true',
        '    components:',
        "      - $defaultComponent"
    )
    if ($environment -eq 'zephyr') {
        $lines += @(
            '    zephyr_config:',
            '      - CONFIG_ZAPEE=y',
            '      - CONFIG_ZAPEE_ZTM_PWM=y',
            '      - CONFIG_ZAPEE_ZTM_PWM_ZEPHYR_ADAPTER=y'
        )
    }

    [System.IO.File]::WriteAllLines($ConfigPath, $lines, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "Created $ConfigPath"
    $config = Read-ZapConfig
    Ensure-CMakeIntegration $config
    Write-GeneratedFiles $config
}

function Resolve-DependencyPath {
    param($Config, [string]$Name, $Dependency)
    $overrideVar = [string]$Dependency.override_var
    if ($overrideVar) {
        $override = [Environment]::GetEnvironmentVariable($overrideVar)
        if ($override) { return [System.IO.Path]::GetFullPath($override) }
    }
    if (([string]$Dependency.type).ToLowerInvariant() -eq 'path') {
        $path = [string]$Dependency.uri
        if ([System.IO.Path]::IsPathRooted($path)) { return [System.IO.Path]::GetFullPath($path) }
        return [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $path))
    }
    $depsDir = Get-ProjectValue $Config 'deps_dir' 'deps'
    if ([System.IO.Path]::IsPathRooted($depsDir)) {
        $depsRoot = [System.IO.Path]::GetFullPath($depsDir)
    } else {
        $depsRoot = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $depsDir))
    }
    return [System.IO.Path]::GetFullPath((Join-Path $depsRoot $Name))
}

function Invoke-Native {
    param([string]$Program, [string[]]$ArgumentList)
    & $Program @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "'$Program' failed with exit code $LASTEXITCODE."
    }
}

function Invoke-NativeCapture {
    param([string]$Program, [string[]]$ArgumentList)
    $output = @(& $Program @ArgumentList 2>&1)
    $exitCode = $LASTEXITCODE
    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = @($output | ForEach-Object { [string]$_ })
    }
}

function Assert-GitCheckoutClean {
    param([string]$Git, [string]$Name, [string]$Path)
    $result = Invoke-NativeCapture -Program $Git -ArgumentList @('-C', $Path, 'status', '--porcelain', '--untracked-files=normal')
    if ($result.ExitCode -ne 0) {
        throw "Unable to inspect dependency '$Name' at '$Path'."
    }
    if ($result.Output.Count -gt 0) {
        $preview = @($result.Output | Select-Object -First 8) -join "`n  "
        throw "Dependency '$Name' has local changes in '$Path'. zap will not overwrite them.`n  $preview`nCommit/stash them, or use a separate local override via its override_var."
    }
}

function Test-GitRevision {
    param([string]$Git, [string]$Path, [string]$Revision)
    $verify = Invoke-NativeCapture -Program $Git -ArgumentList @('-C', $Path, 'rev-parse', '--verify', '--quiet', "${Revision}^{commit}")
    return $verify.ExitCode -eq 0
}

function Resolve-GitRevision {
    param([string]$Git, [string]$Path, [string]$Revision)
    if (Test-GitRevision -Git $Git -Path $Path -Revision $Revision) {
        return $Revision
    }
    $remoteRevision = "origin/$Revision"
    if (Test-GitRevision -Git $Git -Path $Path -Revision $remoteRevision) {
        return $remoteRevision
    }
    return ''
}

function Get-GitValue {
    param([string]$Git, [string]$Path, [string[]]$ArgumentList)
    $result = Invoke-NativeCapture -Program $Git -ArgumentList (@('-C', $Path) + $ArgumentList)
    if ($result.ExitCode -ne 0 -or $result.Output.Count -eq 0) {
        return ''
    }
    return [string]$result.Output[0]
}

function Sync-Dependencies {
    param($Config)
    foreach ($name in $Config.dependencies.Keys) {
        $dep = $Config.dependencies[$name]
        $type = ([string]$dep.type).ToLowerInvariant()
        $path = Resolve-DependencyPath $Config $name $dep
        $overrideVar = [string]$dep.override_var
        if ($overrideVar -and [Environment]::GetEnvironmentVariable($overrideVar)) {
            Write-Host "${name}: local override $path"
            continue
        }

        switch ($type) {
            'git' {
                $git = Get-Command git -ErrorAction SilentlyContinue
                if (-not $git) { throw "git is required to synchronise dependency '$name'." }
                if (-not (Test-Path -LiteralPath (Join-Path $path '.git'))) {
                    $parent = Split-Path -Parent $path
                    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
                    if (Test-Path -LiteralPath $path) {
                        $items = @(Get-ChildItem -LiteralPath $path -Force -ErrorAction SilentlyContinue)
                        if ($items.Count -gt 0) { throw "Dependency directory '$path' exists but is not a Git checkout." }
                    }
                    Write-Host "Cloning $name -> $path"
                    Invoke-Native -Program $git.Source -ArgumentList @('clone', [string]$dep.uri, $path)
                }
                Write-Host "Synchronising $name @ $($dep.version)"
                Assert-GitCheckoutClean -Git $git.Source -Name $name -Path $path
                Invoke-Native -Program $git.Source -ArgumentList @('-C', $path, 'fetch', '--tags', '--prune', 'origin')
                $resolvedRevision = Resolve-GitRevision -Git $git.Source -Path $path -Revision ([string]$dep.version)
                if (-not $resolvedRevision) {
                    throw "Dependency '$name' requested Git ref '$($dep.version)', but that ref was not found after fetching '$($dep.uri)'. Check the version in zap.yml and make sure the tag/branch has been pushed."
                }
                Invoke-Native -Program $git.Source -ArgumentList @('-C', $path, 'checkout', '--detach', $resolvedRevision)
            }
            'path' {
                if (-not (Test-Path -LiteralPath $path)) { throw "Local dependency '$name' was not found at '$path'." }
                Write-Host "${name}: local path $path"
            }
            'url' {
                Write-Host "${name}: URL dependency will be populated by CMake into $path"
            }
            default { throw "Unsupported dependency type '$type'." }
        }
    }
}

function Invoke-ConfigureOnly {
    param($Config)
    $environment = (Get-ProjectValue $Config 'environment' 'generic').ToLowerInvariant()
    $buildDir = Get-ProjectValue $Config 'build_dir' 'build'
    $board = Get-ProjectValue $Config 'board' ''

    switch ($environment) {
        'pico-sdk' {
            $buildScript = Join-Path $ProjectRoot 'build.ps1'
            if (-not (Test-Path -LiteralPath $buildScript)) {
                throw 'pico-sdk environment expects build.ps1 in the project root.'
            }
            # Hashtable splatting is required for named script parameters. Array
            # splatting is positional, which would bind "-ConfigureOnly" to the
            # first parameter (Configuration) instead of treating it as a switch.
            $buildParams = @{
                ConfigureOnly = $true
                BuildDir      = $buildDir
            }
            if ($board) { $buildParams.Board = $board }
            & $buildScript @buildParams
            if ($LASTEXITCODE -ne 0) { throw "build.ps1 configure failed with exit code $LASTEXITCODE." }
        }
        'zephyr' {
            if (-not $board) { throw 'Zephyr projects require project.board in zap.yml.' }
            $west = Get-Command west -ErrorAction SilentlyContinue
            if (-not $west) { throw 'west was not found for the Zephyr environment.' }

            $modulePaths = @()
            foreach ($name in $Config.dependencies.Keys) {
                $dep = $Config.dependencies[$name]
                if (ConvertTo-Bool $dep.zephyr_module) {
                    $modulePaths += Resolve-DependencyPath $Config $name $dep
                }
            }
            $westArgs = @('build', '-b', $board, '-d', $buildDir, '--cmake-only', $ProjectRoot)
            $cmakeArgs = @()
            if ($modulePaths.Count -gt 0) {
                $cmakeArgs += '-DZEPHYR_MODULES=' + ($modulePaths -join ';')
            }
            if (Test-Path -LiteralPath $GeneratedZephyrConfPath) {
                $cmakeArgs += '-DEXTRA_CONF_FILE=' + $GeneratedZephyrConfPath
            }
            if ($cmakeArgs.Count -gt 0) {
                $westArgs += '--'
                $westArgs += $cmakeArgs
            }
            Invoke-Native -Program $west.Source -ArgumentList $westArgs
        }
        'generic' {
            $cmake = Get-Command cmake -ErrorAction SilentlyContinue
            if (-not $cmake) { throw 'cmake was not found.' }
            Invoke-Native -Program $cmake.Source -ArgumentList @('-S', $ProjectRoot, '-B', (Join-Path $ProjectRoot $buildDir), '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON')
        }
        default { throw "Unsupported project.environment '$environment'." }
    }
}

function Write-Config {
    param($Config)
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('# Zap dependency manifest')
    $lines.Add('schema: ' + $Config.schema)
    $lines.Add('')
    $lines.Add('project:')
    foreach ($key in $Config.project.Keys) {
        $lines.Add("  $key`: $($Config.project[$key])")
    }
    $lines.Add('')
    $lines.Add('dependencies:')
    foreach ($name in $Config.dependencies.Keys) {
        $dep = $Config.dependencies[$name]
        $lines.Add("  $name`:")
        foreach ($key in @('type', 'uri', 'version', 'hash', 'override_var', 'zephyr_module')) {
            if ($dep[$key]) { $lines.Add("    $key`: $($dep[$key])") }
        }
        if ($dep.components.Count -gt 0) {
            $lines.Add('    components:')
            foreach ($item in $dep.components) { $lines.Add("      - $item") }
        }
        if ($dep.zephyr_config.Count -gt 0) {
            $lines.Add('    zephyr_config:')
            foreach ($item in $dep.zephyr_config) { $lines.Add("      - $item") }
        }
    }
    [System.IO.File]::WriteAllLines($ConfigPath, $lines, (New-Object System.Text.UTF8Encoding($false)))
}

function New-Adapter {
    param($Config, [string]$Name)
    if (-not $Name) { $Name = Read-Host 'Adapter name (for example stm32, my_board, pico)' }
    $Name = ($Name -replace '[^A-Za-z0-9_]', '_').ToLowerInvariant()
    if (-not $Name) { throw 'Adapter name is required.' }

    $adapterRoot = Get-ProjectValue $Config 'adapters_dir' 'adapters'
    $outDir = Join-Path (Join-Path $ProjectRoot $adapterRoot) $Name
    if (Test-Path -LiteralPath $outDir) { throw "Adapter directory '$outDir' already exists." }
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null

    $h = @"
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zaptronics/modules/ztm_adc.h>
#include <zaptronics/modules/ztm_pwm.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    zap_i2c_bus_t i2c;
    zap_spi_device_t spi;
    zap_lock_t operation_lock;
    zap_time_t time;
    void *platform_context;
} zap_${Name}_adapter_t;

/*
 * Initialise the portable ZapEE interfaces for this platform.
 * Replace the TODO callbacks in zap_${Name}_adapter.c with your SDK/RTOS calls.
 */
int zap_${Name}_adapter_init(zap_${Name}_adapter_t *adapter, void *platform_context);

/* Fill a ZTM_PWM configuration with this adapter plus board-control callbacks. */
void zap_${Name}_ztm_pwm_config(ztm_pwm_config_t *config,
                                zap_${Name}_adapter_t *adapter,
                                void *control_context,
                                ztm_pwm_power_set_fn power_set,
                                ztm_pwm_power_get_fn power_get,
                                ztm_pwm_irq_read_fn irq_read,
                                void *event_context,
                                ztm_pwm_event_fn event);

/* Fill a ZTM_ADC configuration with this adapter's SPI/time/operation lock. */
void zap_${Name}_ztm_adc_config(ztm_adc_config_t *config,
                                zap_${Name}_adapter_t *adapter,
                                void *event_context,
                                ztm_adc_event_fn event);

#ifdef __cplusplus
}
#endif
"@

    $c = @"
// SPDX-License-Identifier: Apache-2.0
#include "zap_${Name}_adapter.h"

#include <string.h>

static int platform_i2c_write(void *context,
                              uint8_t address_7bit,
                              const uint8_t *data,
                              size_t len,
                              uint32_t timeout_us)
{
    (void)context;
    (void)address_7bit;
    (void)data;
    (void)len;
    (void)timeout_us;
    /* TODO: call your platform I2C write API. Return 0 on success, negative on failure. */
    return ZAP_I2C_E_UNSUPPORTED;
}

static int platform_i2c_write_read(void *context,
                                   uint8_t address_7bit,
                                   const uint8_t *write_data,
                                   size_t write_len,
                                   uint8_t *read_data,
                                   size_t read_len,
                                   uint32_t timeout_us)
{
    (void)context;
    (void)address_7bit;
    (void)write_data;
    (void)write_len;
    (void)read_data;
    (void)read_len;
    (void)timeout_us;
    /* TODO: one atomic WRITE -> repeated START -> READ transaction. */
    return ZAP_I2C_E_UNSUPPORTED;
}

static int platform_i2c_probe(void *context, uint8_t address_7bit, uint32_t timeout_us)
{
    (void)context;
    (void)address_7bit;
    (void)timeout_us;
    /* Optional: return 0 when the address ACKs, negative otherwise. */
    return ZAP_I2C_E_UNSUPPORTED;
}

static int platform_i2c_lock(void *context, uint32_t timeout_us)
{
    (void)context;
    (void)timeout_us;
    /* Optional: lock the shared I2C controller. Return 0 on success. */
    return ZAP_I2C_OK;
}

static void platform_i2c_unlock(void *context)
{
    (void)context;
}

static int platform_spi_begin(void *context, uint32_t timeout_us)
{
    (void)context;
    (void)timeout_us;
    /* TODO: lock/configure the shared SPI controller and assert this device's CS. */
    return ZAP_SPI_E_UNSUPPORTED;
}

static int platform_spi_transfer(void *context,
                                 const uint8_t *tx,
                                 uint8_t *rx,
                                 size_t len,
                                 uint8_t read_fill,
                                 uint32_t timeout_us)
{
    (void)context;
    (void)tx;
    (void)rx;
    (void)len;
    (void)read_fill;
    (void)timeout_us;
    /* TODO: transfer while CS remains asserted. Use read_fill for read-only clocks. */
    return ZAP_SPI_E_UNSUPPORTED;
}

static int platform_spi_end(void *context)
{
    (void)context;
    /* TODO: deassert CS and release the shared SPI controller. */
    return ZAP_SPI_E_UNSUPPORTED;
}

static int platform_operation_lock(void *context, uint32_t timeout_us)
{
    (void)context;
    (void)timeout_us;
    /* Optional: lock one module instance across complete multi-register operations. */
    return 0;
}

static void platform_operation_unlock(void *context)
{
    (void)context;
}

static void platform_delay_ms(void *context, uint32_t delay_ms)
{
    (void)context;
    (void)delay_ms;
    /* TODO: call your platform millisecond delay/sleep primitive. */
}

static void platform_delay_us(void *context, uint32_t delay_us)
{
    (void)context;
    (void)delay_us;
    /* TODO: call your platform microsecond delay primitive. */
}

static uint64_t platform_now_us(void *context)
{
    (void)context;
    /* TODO: return a monotonic microsecond timebase. */
    return 0u;
}

static const zap_i2c_bus_ops_t g_i2c_ops = {
    .write = platform_i2c_write,
    .write_read = platform_i2c_write_read,
    .probe = platform_i2c_probe,
    .lock = platform_i2c_lock,
    .unlock = platform_i2c_unlock,
};

static const zap_spi_device_ops_t g_spi_ops = {
    .begin = platform_spi_begin,
    .transfer = platform_spi_transfer,
    .end = platform_spi_end,
};

int zap_${Name}_adapter_init(zap_${Name}_adapter_t *adapter, void *platform_context)
{
    if (adapter == NULL) {
        return -1;
    }
    memset(adapter, 0, sizeof(*adapter));
    adapter->platform_context = platform_context;

    adapter->i2c.context = platform_context;
    adapter->i2c.ops = &g_i2c_ops;
    adapter->i2c.timeout_us = ZAP_I2C_DEFAULT_TIMEOUT_US;

    adapter->spi.context = platform_context;
    adapter->spi.ops = &g_spi_ops;
    adapter->spi.timeout_us = ZAP_SPI_DEFAULT_TIMEOUT_US;
    adapter->spi.read_fill = 0u;

    adapter->operation_lock.context = platform_context;
    adapter->operation_lock.acquire = platform_operation_lock;
    adapter->operation_lock.release = platform_operation_unlock;
    adapter->operation_lock.timeout_us = ZTM_ADC_OPERATION_TIMEOUT_US;

    adapter->time.context = platform_context;
    adapter->time.delay_ms = platform_delay_ms;
    adapter->time.delay_us = platform_delay_us;
    adapter->time.now_us = platform_now_us;
    return 0;
}

void zap_${Name}_ztm_pwm_config(ztm_pwm_config_t *config,
                                zap_${Name}_adapter_t *adapter,
                                void *control_context,
                                ztm_pwm_power_set_fn power_set,
                                ztm_pwm_power_get_fn power_get,
                                ztm_pwm_irq_read_fn irq_read,
                                void *event_context,
                                ztm_pwm_event_fn event)
{
    if (config == NULL || adapter == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->i2c = &adapter->i2c;
    config->time = &adapter->time;
    config->control_context = control_context;
    config->power_set = power_set;
    config->power_get = power_get;
    config->irq_read = irq_read;
    config->event_context = event_context;
    config->event = event;
}

void zap_${Name}_ztm_adc_config(ztm_adc_config_t *config,
                                zap_${Name}_adapter_t *adapter,
                                void *event_context,
                                ztm_adc_event_fn event)
{
    if (config == NULL || adapter == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->spi = &adapter->spi;
    config->time = &adapter->time;
    config->lock = &adapter->operation_lock;
    config->event_context = event_context;
    config->event = event;
}
"@

    $readme = @"
# $Name ZapEE adapter skeleton

Generated by `zap adapter $Name`.

The skeleton exposes the portable interfaces currently used by ZapEE modules:

1. Implement the I2C callbacks for I2C-based modules such as ZTM_PWM. `write_read()` must be one atomic write / repeated-start / read transaction.
2. Implement SPI `begin` / `transfer` / `end` for SPI-based modules such as ZTM_ADC. Keep the selected device active from `begin` until `end` and configure the bus for that endpoint before the first transfer.
3. Replace the bus/operation lock TODOs with your RTOS/OS mutexes when concurrency is possible. For deliberately single-threaded bare metal, set the relevant lock callbacks/pointers to NULL instead.
4. Implement millisecond delay, microsecond delay, and a monotonic microsecond clock.
5. Supply ZTM_PWM power-enable/IRQ callbacks from your board layer when that module is used.
6. Add this `.c` file to your application target; ZapEE itself remains unchanged.

Unused interfaces may be removed from the application-owned skeleton. The generated transport callbacks intentionally return `*_E_UNSUPPORTED` until platform-specific TODOs are implemented.
"@

    [System.IO.File]::WriteAllText((Join-Path $outDir "zap_${Name}_adapter.h"), $h, (New-Object System.Text.UTF8Encoding($false)))
    [System.IO.File]::WriteAllText((Join-Path $outDir "zap_${Name}_adapter.c"), $c, (New-Object System.Text.UTF8Encoding($false)))
    [System.IO.File]::WriteAllText((Join-Path $outDir 'README.md'), $readme, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "Created adapter skeleton in $outDir"
}

function Show-Status {
    param($Config)
    Write-Host "Environment : $(Get-ProjectValue $Config 'environment' 'generic')"
    Write-Host "Target      : $(Get-ProjectValue $Config 'target' '')"
    Write-Host "Dependencies:"

    foreach ($name in $Config.dependencies.Keys) {
        $dep = $Config.dependencies[$name]
        $type = ([string]$dep.type).ToLowerInvariant()
        $path = Resolve-DependencyPath $Config $name $dep
        $overrideVar = [string]$dep.override_var
        $override = if ($overrideVar) { [Environment]::GetEnvironmentVariable($overrideVar) } else { '' }

        Write-Host "  $name"
        Write-Host "    configured : $type $($dep.uri) $($dep.version)"
        Write-Host "    source     : $path"
        if ($override) {
            Write-Host "    override   : $overrideVar=$override"
        }

        if ($type -eq 'git' -and (Test-Path -LiteralPath (Join-Path $path '.git'))) {
            $git = Get-Command git -ErrorAction SilentlyContinue
            if (-not $git) {
                Write-Host '    checkout   : git not found'
                continue
            }
            $origin = Get-GitValue -Git $git.Source -Path $path -ArgumentList @('remote', 'get-url', 'origin')
            $head = Get-GitValue -Git $git.Source -Path $path -ArgumentList @('rev-parse', '--short=12', 'HEAD')
            $dirty = Invoke-NativeCapture -Program $git.Source -ArgumentList @('-C', $path, 'status', '--porcelain', '--untracked-files=normal')
            $state = if ($dirty.ExitCode -ne 0) { 'unknown' } elseif ($dirty.Output.Count -gt 0) { 'DIRTY' } else { 'clean' }
            $match = 'unknown'
            $revision = [string]$dep.version
            $resolvedRevision = Resolve-GitRevision -Git $git.Source -Path $path -Revision $revision
            if ($resolvedRevision) {
                $wanted = Get-GitValue -Git $git.Source -Path $path -ArgumentList @('rev-parse', '--short=12', "${resolvedRevision}^{commit}")
                $match = if ($wanted -and $head -eq $wanted) { 'yes' } else { 'no' }
            } else {
                $match = 'ref not fetched/found'
            }
            Write-Host "    origin     : $origin"
            Write-Host "    HEAD       : $head"
            Write-Host "    worktree   : $state"
            Write-Host "    version ok : $match"
        } elseif ($type -eq 'git') {
            Write-Host '    checkout   : not fetched'
        } elseif (Test-Path -LiteralPath $path) {
            Write-Host '    checkout   : present'
        } else {
            Write-Host '    checkout   : missing'
        }
    }
}

function Show-AvailableVersions {
    param($Config, [string]$Name)
    if (-not $Config.dependencies.Contains($Name)) {
        throw "Unknown dependency '$Name'."
    }
    $dep = $Config.dependencies[$Name]
    if (([string]$dep.type).ToLowerInvariant() -ne 'git') {
        throw "Dependency '$Name' is not a Git dependency."
    }
    $git = Get-Command git -ErrorAction SilentlyContinue
    if (-not $git) { throw 'git is required to query dependency versions.' }

    Write-Host "$Name currently uses $($dep.version)"
    Write-Host "Querying $($dep.uri) ..."
    $result = Invoke-NativeCapture -Program $git.Source -ArgumentList @('ls-remote', '--tags', '--refs', [string]$dep.uri)
    if ($result.ExitCode -ne 0) {
        throw "Unable to query tags for '$Name' from '$($dep.uri)'."
    }

    $tags = @()
    foreach ($line in $result.Output) {
        if ($line -match 'refs/tags/(.+)$') {
            $tags += $Matches[1]
        }
    }
    $releaseTags = @($tags | Where-Object { $_ -match '^v?\d+\.\d+\.\d+$' } |
        Sort-Object -Property @{ Expression = { [version](($_ -replace '^v', '')) }; Descending = $true })

    if ($releaseTags.Count -eq 0) {
        Write-Host 'No x.y.z release tags were found.'
        return
    }
    Write-Host 'Available releases (newest first):'
    foreach ($tag in @($releaseTags | Select-Object -First 12)) {
        $marker = if ($tag -eq [string]$dep.version) { '  *' } else { '   ' }
        Write-Host "$marker $tag"
    }
    Write-Host "Use '.\\zap version $Name <tag>' to select a release."
}

function Show-List {
    param($Config)
    Write-Host "Environment : $(Get-ProjectValue $Config 'environment' 'generic')"
    Write-Host "Target      : $(Get-ProjectValue $Config 'target' '')"
    Write-Host "Dependencies:"
    foreach ($name in $Config.dependencies.Keys) {
        $dep = $Config.dependencies[$name]
        Write-Host "  $name  $($dep.type)  $($dep.uri)  $($dep.version)"
        foreach ($component in $dep.components) { Write-Host "    -> $component" }
    }
}

function Show-Help {
    Write-Host @'
Zap project dependency helper

  .\zap new
      Create zap.yml, add the small CMake integration markers, and generate cmake/zap_deps.cmake.

  .\zap
      Synchronise dependencies, regenerate CMake glue, then run configure only (no compilation).

  .\zap generate
      Regenerate cmake/zap_deps.cmake and cmake/zap_zephyr.conf without fetching/configuring.

  .\zap check
      Verify generated files and CMake integration are present and up to date.

  .\zap list
      Show dependency sources, versions, and selected component targets.

  .\zap status
      Show resolved dependency paths, Git HEADs, dirty state, and whether each checkout matches zap.yml.

  .\zap update <dependency>
      Query the dependency remote and show the newest release tags without changing zap.yml.

  .\zap version <dependency> <version-or-git-ref>
      Change a dependency version/ref in zap.yml and regenerate CMake glue.

  .\zap add <dependency> <cmake-target>
      Add a component target such as zap::ztm_pwm to a dependency.

  .\zap remove <dependency> <cmake-target>
      Remove a component target.

  .\zap adapter [name]
      Generate a portable I2C/SPI/lock/time adapter skeleton for ZTM_PWM/ZTM_ADC in project.adapters_dir.

zap.yml intentionally uses a small, documented YAML subset so this script has no Python,
PowerShell-module, or package-manager dependency. Standard CMake/FetchContent and Zephyr
remain fully supported without using this helper.
'@
}

switch ($Command.ToLowerInvariant()) {
    '' {
        $config = Read-ZapConfig
        Ensure-CMakeIntegration $config
        Write-GeneratedFiles $config
        Sync-Dependencies $config
        Invoke-ConfigureOnly $config
    }
    'new' { Write-NewConfig }
    'generate' {
        $config = Read-ZapConfig
        Ensure-CMakeIntegration $config
        Write-GeneratedFiles $config
    }
    'check' {
        $config = Read-ZapConfig
        $expectedCMake = Get-GeneratedCMakeText $config
        $expectedZephyr = Get-GeneratedZephyrConfText $config
        if (-not (Test-Path -LiteralPath $GeneratedCMakePath)) { throw 'cmake/zap_deps.cmake is missing. Run .\\zap generate.' }
        if ((Normalize-Text (Get-Content -LiteralPath $GeneratedCMakePath -Raw)) -ne (Normalize-Text $expectedCMake)) { throw 'cmake/zap_deps.cmake is out of date. Run .\\zap generate.' }
        if (-not (Test-Path -LiteralPath $GeneratedZephyrConfPath)) { throw 'cmake/zap_zephyr.conf is missing. Run .\\zap generate.' }
        if ((Normalize-Text (Get-Content -LiteralPath $GeneratedZephyrConfPath -Raw)) -ne (Normalize-Text $expectedZephyr)) { throw 'cmake/zap_zephyr.conf is out of date. Run .\\zap generate.' }
        $cmakeText = Get-Content -LiteralPath $CMakePath -Raw
        if (-not $cmakeText.Contains('# ZAP MANAGED DEPENDENCIES BEGIN') -or -not $cmakeText.Contains('# ZAP MANAGED TARGETS BEGIN')) {
            throw 'CMakeLists.txt is missing Zap integration markers. Run .\\zap generate to repair it.'
        }
        Write-Host 'zap.yml, generated CMake glue, and CMake integration are in sync.'
    }
    'list' { Show-List (Read-ZapConfig) }
    'status' { Show-Status (Read-ZapConfig) }
    'update' {
        if ($Arguments.Count -lt 1) { throw 'Usage: .\zap update <dependency>' }
        Show-AvailableVersions (Read-ZapConfig) $Arguments[0]
    }
    'version' {
        if ($Arguments.Count -lt 2) { throw 'Usage: .\\zap version <dependency> <version-or-ref>' }
        $config = Read-ZapConfig
        $name = $Arguments[0]
        if (-not $config.dependencies.Contains($name)) { throw "Unknown dependency '$name'." }
        $config.dependencies[$name].version = $Arguments[1]
        Write-Config $config
        Write-GeneratedFiles $config
        Write-Host "$name now uses $($Arguments[1])."
    }
    'add' {
        if ($Arguments.Count -lt 2) { throw 'Usage: .\\zap add <dependency> <cmake-target>' }
        $config = Read-ZapConfig
        $name = $Arguments[0]
        $target = $Arguments[1]
        if (-not $config.dependencies.Contains($name)) { throw "Unknown dependency '$name'." }
        if ($config.dependencies[$name].components -notcontains $target) {
            $config.dependencies[$name].components += $target
            Write-Config $config
            Write-GeneratedFiles $config
        }
        Write-Host "$name includes $target."
    }
    'remove' {
        if ($Arguments.Count -lt 2) { throw 'Usage: .\\zap remove <dependency> <cmake-target>' }
        $config = Read-ZapConfig
        $name = $Arguments[0]
        $target = $Arguments[1]
        if (-not $config.dependencies.Contains($name)) { throw "Unknown dependency '$name'." }
        $config.dependencies[$name].components = @($config.dependencies[$name].components | Where-Object { $_ -ne $target })
        Write-Config $config
        Write-GeneratedFiles $config
        Write-Host "$name no longer includes $target."
    }
    'adapter' {
        $config = Read-ZapConfig
        $name = if ($Arguments.Count -gt 0) { $Arguments[0] } else { '' }
        New-Adapter $config $name
    }
    'help' { Show-Help }
    '--help' { Show-Help }
    '-h' { Show-Help }
    default {
        Show-Help
        throw "Unknown zap command '$Command'."
    }
}
