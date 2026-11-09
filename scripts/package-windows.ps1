#requires -Version 5.1
<#
.SYNOPSIS
Build Windows and Linux x64 clients and the Linux dedicated server on Windows.
Clients are uploaded with Butler, the server as a Docker image to Docker Hub.
.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1 -InitAssets
.EXAMPLE
.\scripts\package-windows.ps1 -Check -Engine 'D:\Epic Games\UE_5.8' -ServerEngine 'D:\UnrealSource\UE_5.8'
.EXAMPLE
.\scripts\package-windows.ps1 -TargetPlatform Windows,Linux
.NOTES
Uses an external build workspace with shared Content; close Unreal Editor first.
Creates/validates asset junctions automatically after tool checks pass.
Uploads unless -NoUpload is passed. -Check never builds or uploads.
Server engine: -ServerEngine, else UE_SERVER_ENGINE_ROOT, else the first UE 5.8.3 able to
build Linux Server among the client engine and engines registered by UnrealVersionSelector.
#>
[CmdletBinding()]
param(
    [string]$Engine,
    [ValidateSet('Development', 'Shipping')][string]$Configuration = 'Development',
    [string]$OutputRoot,
    [switch]$InitAssets,
    [switch]$Check,
    [string]$Butler = 'butler.exe',
    [string]$ButlerIdentity,
    [switch]$NoUpload,
    [string]$UploadOnly,
    [ValidateSet('All', 'Both', 'Windows', 'Linux', 'Server')][string[]]$TargetPlatform = @('All'),
    [string]$LinuxToolchain,
    [string]$ServerEngine,
    [string]$Docker = 'docker.exe',
    [string]$DockerCredentials,
    [string]$DockerRepository
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$projectRoot = Split-Path $PSScriptRoot -Parent
$uproject = Join-Path $projectRoot 'RespawnAndRage.uproject'
$problems = New-Object 'System.Collections.Generic.List[string]'
$logFile = $null
$resolvedEngine = $null
$resolvedServerEngine = $null
$previousLinuxRoot = $env:LINUX_MULTIARCH_ROOT
$serverImageScript = Join-Path $PSScriptRoot 'server-image.py'
if (-not $DockerCredentials) { $DockerCredentials = Join-Path $projectRoot '.secrets\dockerhub.json' }
# 'Both' keeps its historical meaning (the two clients); 'All' adds the server.
$selected = @(foreach ($item in $TargetPlatform) {
    switch ($item) { 'All' { 'Windows', 'Linux', 'Server' } 'Both' { 'Windows', 'Linux' } default { $item } }
})
$platforms = @(@('Windows', 'Linux', 'Server') | Where-Object { $_ -in $selected })

function Test-Dependency([string]$Name, [scriptblock]$Action) {
    try {
        & $Action
        Write-Host "[OK] $Name" -ForegroundColor Green
    } catch {
        $problems.Add("${Name}: $($_.Exception.Message)")
        Write-Host "[MISSING] ${Name}: $($_.Exception.Message)" -ForegroundColor Red
    }
}

function Invoke-Native([string]$FilePath, [string[]]$Arguments, [switch]$Capture) {
    # Resolve before relaxing ErrorActionPreference: a missing executable must
    # never reuse the exit code from an earlier successful native command.
    # Multiple Python installations/App Execution Aliases can produce several
    # applications. Invoke just the first PATH match, never an array of paths.
    $command = Get-Command -Name $FilePath -CommandType Application -ErrorAction Stop | Select-Object -First 1
    # Windows PowerShell 5.1 turns native stderr into ErrorRecords. Normalize
    # them to plain messages before display/logging, avoiding misleading red
    # NativeCommandError stack traces. The exit code still determines failure.
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        if ($Capture) {
            $output = @(& $command.Source @Arguments 2>&1 | ForEach-Object { [string]$_ })
            $output | Out-Host
            if ($logFile) { $output | Out-File -LiteralPath $logFile -Append }
        } elseif ($logFile) {
            & $command.Source @Arguments 2>&1 | ForEach-Object { [string]$_ } | Tee-Object -FilePath $logFile -Append | Out-Host
        } else {
            & $command.Source @Arguments 2>&1 | ForEach-Object { [string]$_ } | Out-Host
        }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($code -ne 0) { throw "$FilePath failed (exit $code). See the output above." }
    if ($Capture) { return ($output -join "`n") }
}

function Invoke-AssetSetup([string]$SetupScript, [switch]$ReadOnly, [switch]$FetchAssets,
        [bool]$CanPrepare = $true) {
    $arguments = @($SetupScript)
    if ($ReadOnly -or -not $CanPrepare) {
        $arguments += '--check'
        if (-not $ReadOnly) { Write-Host 'Automatic asset setup deferred until the tool checks above pass.' }
    } elseif ($FetchAssets) {
        $arguments += '--init'
    }
    # No flag: safely mount already-downloaded assets and validate the result.
    # setup-assets.py preserves correct links and refuses conflicting data/LFS
    # pointers before making any junction. Fetching remains explicit opt-in.
    Invoke-Native 'python.exe' $arguments
}

function Find-Engine {
    if ($Engine) { return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Engine) }
    $candidates = @($env:UE_ENGINE_ROOT)
    $registered = Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8' -ErrorAction SilentlyContinue
    if ($registered) { $candidates += $registered.InstalledDirectory }
    $launcherFile = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path -LiteralPath $launcherFile) {
        $launcher = Get-Content -LiteralPath $launcherFile -Raw | ConvertFrom-Json
        $candidates += @($launcher.InstallationList | Where-Object AppName -eq 'UE_5.8' | ForEach-Object InstallLocation)
    }
    $candidates += @('C:\Unreal\UE_5.8', (Join-Path $env:ProgramFiles 'Epic Games\UE_5.8'))
    foreach ($candidate in $candidates) {
        if (-not $candidate) { continue }
        $versionFile = Join-Path $candidate 'Engine\Build\Build.version'
        if (Test-Path -LiteralPath $versionFile) {
            $version = Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json
            if ("$($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)" -eq '5.8.3') {
                return [IO.Path]::GetFullPath($candidate)
            }
        }
    }
    throw 'Install UE 5.8.3 in Epic Launcher, or specify -Engine ''D:\Epic Games\UE_5.8''.'
}

function Get-ServerEngineProblem([string]$EngineRoot) {
    # $null when the engine can build the Linux server, otherwise the reason (output stays quiet).
    $python = Get-Command -Name 'python.exe' -CommandType Application -ErrorAction Stop | Select-Object -First 1
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& $python.Source $serverImageScript 'check-engine' '--engine' $EngineRoot 2>&1 | ForEach-Object { [string]$_ })
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($code -eq 0) { return $null }
    $reason = (@($output | Where-Object { $_.Trim() }) -join ' ').Trim()
    if (-not $reason) { $reason = "check-engine failed (exit $code)" }
    return $reason
}

function Find-ServerEngine([string]$ClientEngine) {
    # An explicit choice (parameter or UE_SERVER_ENGINE_ROOT) never falls back silently.
    $explicit = $ServerEngine
    if (-not $explicit) { $explicit = $env:UE_SERVER_ENGINE_ROOT }
    if ($explicit) {
        $path = [IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($explicit)).TrimEnd('\', '/')
        $problem = Get-ServerEngineProblem $path
        if (-not $problem) { return $path }
        throw "Server engine $path cannot build the Linux server: $problem. Use a UE 5.8.3 source engine (after Setup.bat), or skip the server with -TargetPlatform Windows,Linux."
    }
    # The client engine first (no second workspace), then engines registered by
    # UnrealVersionSelector, which is where source builds appear.
    $candidates = @()
    if ($ClientEngine) { $candidates += $ClientEngine }
    $builds = Get-ItemProperty 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
    if ($builds) {
        $candidates += @($builds.PSObject.Properties | Where-Object { $_.Name -notlike 'PS*' -and $_.Value } |
            ForEach-Object { [string]$_.Value })
    }
    $tried = @()
    foreach ($candidate in $candidates) {
        try { $path = [IO.Path]::GetFullPath($candidate).TrimEnd('\', '/') } catch { continue }
        if ($tried -contains $path) { continue }
        $tried += $path
        $versionFile = Join-Path $path 'Engine\Build\Build.version'
        if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) { continue }
        $version = Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json
        if ("$($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)" -ne '5.8.3') { continue }
        $problem = Get-ServerEngineProblem $path
        if (-not $problem) { return $path }
        Write-Host "Server engine skipped: $path ($problem)"
    }
    throw ('No UE 5.8.3 engine able to build Linux Server targets (Epic Launcher engines never can). Pass -ServerEngine ' +
        'or set UE_SERVER_ENGINE_ROOT to a source engine (after Setup.bat), register it with UnrealVersionSelector, ' +
        'or skip the server with -TargetPlatform Windows,Linux.')
}

function Assert-FreeSpace([string]$Path, [double]$MinimumGiB) {
    $existing = $Path
    while (-not (Test-Path -LiteralPath $existing)) {
        $parent = Split-Path $existing -Parent
        if (-not $parent -or $parent -eq $existing) { throw "No accessible local drive for $Path." }
        $existing = $parent
    }
    $drive = (Get-Item -LiteralPath $existing).PSDrive
    if ($null -eq $drive.Free) { throw "Cannot check free space on $Path. Use a local drive." }
    if ($drive.Free -lt $MinimumGiB * 1GB) {
        throw ('Need at least {0} GiB free on {1}; available: {2:N1} GiB.' -f $MinimumGiB, $drive.Root, ($drive.Free / 1GB))
    }
}

function Assert-WindowsPrerequisites([string]$EngineRoot) {
    # UE 5.8 WinPlatform.Automation stages these with -prereqs. The old
    # UEPrereqSetup_x64.exe reference is commented out in that engine version.
    foreach ($name in @('vc_redist.x64.exe', 'vc_redist.arm64.exe')) {
        $path = Join-Path $EngineRoot "Engine\Extras\Redist\en-us\$name"
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Engine found at $EngineRoot; missing $path for Windows -prereqs. Verify UE 5.8.3 in Epic Launcher."
        }
    }
}

function Get-MSVCVersion([IO.DirectoryInfo]$Toolchain) {
    # Same product-version fields used by UBT IsValidToolChainDirMSVC; folder
    # family and compiler patch version can differ after a VS servicing update.
    $info = (Get-Item -LiteralPath (Join-Path $Toolchain.FullName 'bin\Hostx64\x64\cl.exe')).VersionInfo
    if ($info.ProductMajorPart) {
        return [version]::new($info.ProductMajorPart, $info.ProductMinorPart, $info.ProductBuildPart)
    }
    return [version]$Toolchain.Name
}

function Test-VersionRange([version]$Version, [string]$Range) {
    $bounds = $Range.Split('-')
    return $Version -ge [version]$bounds[0] -and $Version -le [version]$bounds[1]
}

function Assert-MSVC([string]$EngineRoot) {
    $sdk = Get-Content -LiteralPath (Join-Path $EngineRoot 'Engine\Config\Windows\Windows_SDK.json') -Raw | ConvertFrom-Json
    $minimum = [version]$sdk.MinimumVisualCppVersion
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Install Visual Studio 2022/2026 or Build Tools with C++ desktop/game development and matching ATL.'
    }
    $installations = @(Invoke-Native $vswhere @('-products', '*', '-version', '[17.8,19.0)', '-property', 'installationPath') -Capture)
    $installations = @($installations -split '\r?\n' | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
    $usable = @()
    $detected = @()
    foreach ($installation in $installations) {
        $toolchains = @(Get-ChildItem (Join-Path $installation 'VC\Tools\MSVC\*') -Directory -ErrorAction SilentlyContinue)
        foreach ($toolchain in $toolchains) {
            $reason = $null
            $version = $null
            foreach ($file in @('bin\Hostx64\x64\cl.exe', 'bin\Hostx64\x64\link.exe',
                    'include\vector', 'lib\x64\libcmt.lib', 'atlmfc\include\atlbase.h', 'atlmfc\lib\x64\atls.lib')) {
                if (-not (Test-Path -LiteralPath (Join-Path $toolchain.FullName $file))) { $reason = "missing $file"; break }
            }
            if (-not $reason) {
                $version = Get-MSVCVersion $toolchain
                if ($version -lt $minimum) { $reason = "compiler $version below engine minimum $minimum" }
                foreach ($range in $sdk.BannedVisualCppVersions) {
                    if (Test-VersionRange $version $range) { $reason = "compiler $version rejected by Unreal ($range)" }
                }
            }
            if ($reason) {
                $detected += "$($toolchain.FullName): $reason"
            } else {
                $usable += "$($toolchain.FullName) (compiler $version)"
            }
        }
    }
    foreach ($item in $detected) { Write-Host "MSVC unavailable: $item" }
    if (-not $usable.Count) {
        throw ('No compatible MSVC x64 + ATL. In Visual Studio Installer add/update C++ tools and matching ATL. ' +
            'UE 5.8.3 accepts e.g. 14.44.35211+ (VS 2022) or 14.50.35723+ (VS 2026); see detected versions above.')
    }
    foreach ($item in $usable) { Write-Host "Compatible MSVC: $item" }
}

function Assert-EnabledPlugins([string]$EngineRoot, [string]$ProjectRoot) {
    $spec = Get-Content -LiteralPath (Join-Path $ProjectRoot 'RespawnAndRage.uproject') -Raw | ConvertFrom-Json
    $enginePlugins = @(Get-ChildItem (Join-Path $EngineRoot 'Engine\Plugins') -Filter '*.uplugin' -Recurse -File)
    $projectPlugins = @(Get-ChildItem (Join-Path $ProjectRoot 'Plugins') -Filter '*.uplugin' -Recurse -File -ErrorAction SilentlyContinue)
    foreach ($reference in @($spec.Plugins | Where-Object Enabled)) {
        # Never call this variable $matches: -match overwrites PowerShell's
        # automatic $Matches hashtable, losing the selected FileInfo objects.
        $pluginCandidates = @($projectPlugins | Where-Object BaseName -eq $reference.Name)
        if (-not $pluginCandidates.Count) { $pluginCandidates = @($enginePlugins | Where-Object BaseName -eq $reference.Name) }
        if ($pluginCandidates.Count -ne 1) { throw "Plugin $($reference.Name) missing or ambiguous. Install the UE 5.8 Windows version (engine or project Plugins folder)." }
        $descriptor = $pluginCandidates[0]
        $plugin = Get-Content -LiteralPath $descriptor.FullName -Raw | ConvertFrom-Json
        if (($descriptor.FullName -match '[/\\]Marketplace[/\\]' -or $descriptor -in $projectPlugins) -and $plugin.Modules) {
            if (-not (Test-Path -LiteralPath (Join-Path $descriptor.DirectoryName 'Source'))) { throw "Plugin source required: $($reference.Name)." }
        }
        if (($platforms -contains 'Linux' -or $platforms -contains 'Server') -and $plugin.SupportedTargetPlatforms -and 'Linux' -notin $plugin.SupportedTargetPlatforms) {
            throw "Plugin $($reference.Name) does not support Linux. Install a compatible source version."
        }
        if ($reference.Name -eq 'PCGExtendedToolkit') {
            if ($plugin.VersionName -ne '0.79.2') { throw "PCGExtendedToolkit 0.79.2 required; found $($plugin.VersionName)." }
            if (-not (Test-Path -LiteralPath (Join-Path $descriptor.DirectoryName 'Source'))) {
                throw 'Install PCGExtendedToolkit with Source, needed to compile the Game target.'
            }
        }
    }
}

function Invoke-EditorBuild([string]$EngineRoot, [string]$ProjectPath) {
    Invoke-Native (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') @(
        'RespawnAndRageEditor', 'Win64', 'Development', "-Project=$ProjectPath", '-NoHotReloadFromIDE', '-WaitMutex')
}

function Invoke-PackageBuild([string]$EngineRoot, [string]$ProjectPath, [string]$ArchivePath, [string]$BuildConfiguration,
        [ValidateSet('Windows', 'Linux')][string]$Platform = 'Windows') {
    $unrealPlatform = 'Win64'
    if ($Platform -eq 'Linux') { $unrealPlatform = 'Linux' }
    $extra = @('-prereqs')
    if ($Platform -eq 'Linux') { $extra = @('-clientarchitecture=x64') }
    Invoke-Native (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') (@(
        'BuildCookRun', "-project=$ProjectPath", '-target=RespawnAndRage', "-platform=$unrealPlatform",
        "-clientconfig=$BuildConfiguration", '-nop4', '-unattended', '-utf8output', '-installed',
        '-build', '-cook', '-stage', '-package', '-pak', '-iostore', '-skipbuildeditor',
        # Use ProjectPackagingSettings.MapsToCook; -map overrides that list.
        '-archive', "-archivedirectory=$ArchivePath") + $extra)
}

function Invoke-ServerPackageBuild([string]$EngineRoot, [string]$ProjectPath, [string]$ArchivePath) {
    # The dedicated server is always Linux x64 Development (server-image.py and
    # run-server-linux.py validate exactly that receipt and executable).
    $arguments = @('BuildCookRun', "-project=$ProjectPath", '-target=RespawnAndRageServer',
        '-platform=Linux', '-serverplatform=Linux', '-server', '-noclient', '-serverconfig=Development',
        '-serverarchitecture=x64', '-nop4', '-unattended', '-utf8output',
        '-build', '-cook', '-stage', '-package', '-pak', '-skipbuildeditor',
        # Use ProjectPackagingSettings.MapsToCook; -map overrides that list.
        '-archive', "-archivedirectory=$ArchivePath")
    # A source engine must not be treated as an Installed Build.
    if (Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine\Build\InstalledBuild.txt')) { $arguments += '-installed' }
    Invoke-Native (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') $arguments
}

function Get-DockerRepository {
    if ($DockerRepository) { return $DockerRepository }
    # Read only the public repository name; the token stays inside server-image.py.
    if (Test-Path -LiteralPath $DockerCredentials -PathType Leaf) {
        $repository = (Get-Content -LiteralPath $DockerCredentials -Raw | ConvertFrom-Json).repository
        if ($repository) { return [string]$repository }
    }
    if ($NoUpload) { return 'local/respawn-and-rage-server' }
    if (-not (Test-Path -LiteralPath $DockerCredentials -PathType Leaf)) {
        throw "Docker Hub credentials missing: $DockerCredentials (see docs/PACKAGE_DESKTOP.md), or pass -NoUpload."
    }
    throw "Set ""repository"" in $DockerCredentials or pass -DockerRepository namespace/name."
}

function Get-DockerArguments([switch]$Upload) {
    @('--docker', $Docker, '--repository', (Get-DockerRepository))
    if ($Upload) {
        '--credentials'
        $DockerCredentials
    }
}

function Assert-DockerServer([switch]$Upload) {
    $arguments = @($serverImageScript, 'check-docker') + @(Get-DockerArguments -Upload:$Upload)
    try { Invoke-Native 'python.exe' $arguments } catch {
        throw 'Docker check failed (see the message above). Docker Desktop with Linux containers is required for the server image.'
    }
}

function Assert-ServerPackage([string]$ArchivePath, [string]$ExpectedVersion) {
    # Validates launcher, settings, executable and Docker context; no Docker call.
    Invoke-Native 'python.exe' @($serverImageScript, 'validate', '--archive', $ArchivePath, '--version', $ExpectedVersion)
}

function Invoke-ServerImagePush([string]$ArchivePath, [string]$ExpectedVersion, [string]$Commit) {
    $arguments = @($serverImageScript, 'publish', '--archive', $ArchivePath, '--version', $ExpectedVersion) +
        @(Get-DockerArguments -Upload)
    if ($Commit) { $arguments += @('--commit', $Commit) }
    Invoke-Native 'python.exe' $arguments
    Write-Host "DOCKER_PUSH_PASS: $(Get-DockerRepository):$ExpectedVersion" -ForegroundColor Green
}

function Assert-Elf64([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try { $header = New-Object byte[] 20; $count = $stream.Read($header, 0, 20) } finally { $stream.Dispose() }
    if ($count -ne 20 -or $header[0] -ne 127 -or $header[1] -ne 69 -or $header[2] -ne 76 -or $header[3] -ne 70 -or
            $header[4] -ne 2 -or $header[5] -ne 1 -or [BitConverter]::ToUInt16($header, 16) -notin 2, 3 -or [BitConverter]::ToUInt16($header, 18) -ne 62) {
        throw "Expected a Linux x86_64 ELF binary: $Path"
    }
}

function Assert-LinuxToolchain([string]$EngineRoot) {
    $sdk = Get-Content -LiteralPath (Join-Path $EngineRoot 'Engine\Config\Linux\Linux_SDK.json') -Raw | ConvertFrom-Json
    $requiredVersion = 'v26_clang-20.1.8-rockylinux8'
    if ($sdk.MainVersion -ne $requiredVersion) { throw "Unexpected engine Linux SDK requirement: $($sdk.MainVersion). UE 5.8.3/v26 required." }
    $root = $LinuxToolchain
    if (-not $root) { $root = $env:LINUX_MULTIARCH_ROOT }
    if (-not $root) { $root = Join-Path $EngineRoot "Engine\Extras\ThirdPartyNotUE\SDKs\HostWin64\Linux\$requiredVersion" }
    $root = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($root)
    $marker = Join-Path $root 'ToolchainVersion.txt'
    if (-not (Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -Raw).Trim() -ne $requiredVersion) {
        throw "Install Epic $requiredVersion, reopen PowerShell, and check LINUX_MULTIARCH_ROOT or pass -LinuxToolchain 'C:\UnrealToolchains\$requiredVersion'."
    }
    $arch = Join-Path $root 'x86_64-unknown-linux-gnu'
    foreach ($file in @('bin\clang++.exe', 'bin\ld.lld.exe', 'bin\llvm-ar.exe', 'bin\llvm-objcopy.exe',
            'include\c++\v1\vector', 'lib64\libc++.a', 'lib64\libc++abi.a')) {
        if (-not (Test-Path -LiteralPath (Join-Path $arch $file))) { throw "Incomplete Linux toolchain: missing $file in $arch." }
    }
    $compiler = Join-Path $arch 'bin\clang++.exe'
    $compilerVersion = Invoke-Native $compiler @('--version') -Capture
    if ($compilerVersion -notmatch 'clang version 20\.1\.8\b') { throw 'Linux toolchain must run clang 20.1.8.' }
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ('rr-linux-toolchain-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temporary | Out-Null
    try {
        $source = Join-Path $temporary 'smoke.cpp'
        $binary = Join-Path $temporary 'smoke'
        '#include <vector>', 'static_assert(sizeof(void*) == 8);', 'int main() { std::vector<int> v{42}; return v[0] == 42 ? 0 : 1; }' |
            Set-Content -LiteralPath $source -Encoding ASCII
        Invoke-Native $compiler @('--target=x86_64-unknown-linux-gnu', "--sysroot=$arch", '-std=c++20', '-fuse-ld=lld',
            '-nostdinc++', '-isystem', (Join-Path $arch 'include'), '-isystem', (Join-Path $arch 'include\c++\v1'),
            "-L$(Join-Path $arch 'lib64')", $source, '-o', $binary, '-nodefaultlibs',
            (Join-Path $arch 'lib64\libc++.a'), (Join-Path $arch 'lib64\libc++abi.a'), '-lm', '-lc', '-lpthread', '-lgcc_s', '-lgcc')
        Assert-Elf64 $binary
    } finally { Remove-Item -LiteralPath $temporary -Recurse -Force }
    # Match the path UBT will use, including an explicit -LinuxToolchain override.
    $env:LINUX_MULTIARCH_ROOT = $root
    Write-Host "Linux toolchain: $root (C++20 link and ELF x86_64 verified)"
}

function Assert-LinuxSupport([string]$EngineRoot) {
    $modules = @(Get-ChildItem (Join-Path $EngineRoot 'Engine\Binaries\Win64') -Recurse -Filter 'UnrealEditor-LinuxTargetPlatform.dll' -File)
    $target = 'UnrealGame.target'
    if ($Configuration -eq 'Shipping') { $target = 'UnrealGame-Linux-Shipping.target' }
    if (-not $modules.Count -or -not (Test-Path -LiteralPath (Join-Path $EngineRoot "Engine\Binaries\Linux\$target"))) {
        throw 'In Epic Launcher, enable Linux target support for UE 5.8.3 and verify the engine installation.'
    }
}

function Get-ButlerArguments {
    if ($ButlerIdentity) {
        '--identity'
        $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ButlerIdentity)
    }
}

function Assert-Butler {
    try { Invoke-Native $Butler @('version') } catch {
        throw 'Install Butler for Windows and add it to PATH, or pass -Butler ''C:\Tools\butler.exe''. See https://itch.io/docs/butler/installing.html.'
    }
    # Check presence only; never read or print the secret. Avoid triggering an
    # interactive login in the middle of an unattended build.
    if ([string]::IsNullOrWhiteSpace($env:BUTLER_API_KEY)) {
        $identity = Join-Path $env:USERPROFILE '.config\itch\butler_creds'
        if ($ButlerIdentity) { $identity = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ButlerIdentity) }
        if (-not (Test-Path -LiteralPath $identity -PathType Leaf) -or (Get-Item -LiteralPath $identity).Length -eq 0) {
            throw 'Run butler login first (with --identity PATH if using -ButlerIdentity), or supply BUTLER_API_KEY through your environment.'
        }
    }
    try {
        # Read-only API check; the Windows channel need not exist yet.
        Invoke-Native $Butler (@(Get-ButlerArguments) + @('status', 'aaggeeaa/respawn-and-rage'))
    } catch {
        throw 'Butler could not access aaggeeaa/respawn-and-rage. Check your connection and butler login credentials.'
    }
}

function Get-GamePackage([string]$ArchivePath, [string]$ExpectedVersion,
        [ValidateSet('Windows', 'Linux')][string]$Platform = 'Windows') {
    # UAT normally archives into dist/Windows. Select the staged launcher root,
    # excluding the second executable under RespawnAndRage/Binaries/Win64.
    $launcherName = 'RespawnAndRage.exe'
    if ($Platform -eq 'Linux') { $launcherName = 'RespawnAndRage.sh' }
    $launchers = @(Get-ChildItem -LiteralPath $ArchivePath -Recurse -File -Filter $launcherName | Where-Object {
        Test-Path -LiteralPath (Join-Path $_.DirectoryName 'RespawnAndRage\Content\Paks') -PathType Container
    })
    if ($launchers.Count -ne 1) { throw "Expected one staged $Platform game with $launcherName and cooked Content/Paks." }
    $directory = $launchers[0].DirectoryName
    $stamp = Join-Path $directory 'RespawnAndRage\Build\GameVersion.txt'
    $version = (Get-Content -LiteralPath $stamp -Raw).Trim()
    if ($version -notmatch '^\d{2}\.\d{2}\.\d{2}\.[0-9a-f]{8}$') { throw 'Invalid packaged game version.' }
    if ($ExpectedVersion -and $version -ne $ExpectedVersion) { throw 'Packaged game version differs from this build.' }
    $containers = @(Get-ChildItem -LiteralPath (Join-Path $directory 'RespawnAndRage\Content\Paks') -File | Where-Object Extension -in '.pak', '.utoc')
    if (-not $containers.Count) { throw 'Cooked game containers are missing.' }
    if ($Platform -eq 'Linux') {
        $binaries = @(Get-ChildItem -LiteralPath (Join-Path $directory 'RespawnAndRage\Binaries\Linux') -File | Where-Object {
            $_.Name -match '^RespawnAndRage(?:-Linux-(?:Development|Shipping))?$'
        })
        if ($binaries.Count -ne 1) { throw 'Expected one Linux game executable.' }
        Assert-Elf64 $binaries[0].FullName
        $launcher = Get-Content -LiteralPath $launchers[0].FullName -Raw
        if (-not $launcher.StartsWith('#!') -or $launcher.Contains("`r")) { throw 'Linux launcher must have a shebang and LF line endings.' }
    }
    [pscustomobject]@{ Directory = $directory; Version = $version }
}

function Get-ItchTarget([ValidateSet('Windows', 'Linux')][string]$Platform) {
    if ($Platform -eq 'Linux') { return 'aaggeeaa/respawn-and-rage:linux-x86_64' }
    return 'aaggeeaa/respawn-and-rage:windows-x86_64'
}

function Invoke-ItchUpload([string]$ArchivePath, [string]$ExpectedVersion,
        [ValidateSet('Windows', 'Linux')][string]$Platform = 'Windows') {
    $package = Get-GamePackage $ArchivePath $ExpectedVersion $Platform
    $itchTarget = Get-ItchTarget $Platform
    $uploadReceiptFile = Join-Path (Split-Path $ArchivePath -Parent) 'itch-upload.json'
    $uploadReceipt = [ordered]@{
        status = 'uploading'; target = $itchTarget; version = $package.Version
        directory = $package.Directory; started = (Get-Date).ToString('o')
    }
    try {
        Write-Host "Uploading $($package.Version) to $itchTarget"
        $pushArguments = @(Get-ButlerArguments) + @('push', $package.Directory, $itchTarget, '--userversion', $package.Version)
        if ($Platform -eq 'Linux') { $pushArguments += '--fix-permissions' }
        Invoke-Native $Butler $pushArguments
        $uploadReceipt.status = 'uploaded'
        Write-Host "ITCH_UPLOAD_PASS: $itchTarget ($($package.Version))" -ForegroundColor Green
    } catch {
        $uploadReceipt.status = 'failed'
        $uploadReceipt['error'] = $_.Exception.Message
        throw
    } finally {
        $uploadReceipt['finished'] = (Get-Date).ToString('o')
        $uploadReceipt | ConvertTo-Json | Set-Content -LiteralPath $uploadReceiptFile -Encoding UTF8
    }
}

function Remove-UploadedBuild([string]$Directory, [switch]$CompletedRetry) {
    if ($CompletedRetry) {
        $summaryPath = Join-Path $Directory 'summary.json'
        $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
        if (-not $summary.platforms -or @($summary.platforms | Where-Object { $_ -notin 'Windows', 'Linux', 'Server' }).Count) {
            throw 'Invalid desktop build summary; refusing cleanup.'
        }
        # Successful attempts were removed; any remaining attempt still needs attention.
        foreach ($platform in $summary.platforms) {
            if (Test-Path -LiteralPath (Join-Path $Directory $platform.ToLowerInvariant())) { return }
        }
        $summary.status = 'uploaded'
        $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    }
    # Python rmtree preserves junction targets, including shared Content mounts.
    $script:logFile = $null
    Invoke-Native 'python.exe' @((Join-Path $PSScriptRoot 'build_cleanup.py'), '--uploaded-run', $Directory)
}

function Invoke-AttemptUpload([string]$Attempt, [switch]$ValidateOnly) {
    $receiptFile = Join-Path $Attempt 'receipt.json'
    $receipt = Get-Content -LiteralPath $receiptFile -Raw | ConvertFrom-Json
    $platform = $receipt.platform
    if (-not $platform) { $platform = 'Windows' } # Legacy Windows-only receipts.
    if ($platform -notin 'Windows', 'Linux', 'Server' -or $receipt.status -notin 'packaged', 'upload_failed', 'uploaded' -or -not $receipt.game_version) {
        throw "Not a completed Windows/Linux/Server package: $Attempt"
    }
    $archive = Join-Path $Attempt 'dist'
    if ($platform -eq 'Server') { Assert-ServerPackage $archive $receipt.game_version }
    else { $null = Get-GamePackage $archive $receipt.game_version $platform }
    if ($ValidateOnly) { return }
    $script:logFile = Join-Path $Attempt 'upload.log'
    try {
        if ($platform -eq 'Server') {
            $receipt | Add-Member -NotePropertyName docker_image -NotePropertyValue "$(Get-DockerRepository):$($receipt.game_version)" -Force
            Invoke-ServerImagePush $archive $receipt.game_version $receipt.commit
        } else {
            $receipt | Add-Member -NotePropertyName itch_target -NotePropertyValue (Get-ItchTarget $platform) -Force
            Invoke-ItchUpload $archive $receipt.game_version $platform
        }
        $receipt.status = 'uploaded'
        $receipt.PSObject.Properties.Remove('error')
    } catch {
        $receipt.status = 'upload_failed'
        $receipt | Add-Member -NotePropertyName error -NotePropertyValue $_.Exception.Message -Force
        throw
    } finally {
        $receipt.finished = (Get-Date).ToString('o')
        $receipt | ConvertTo-Json | Set-Content -LiteralPath $receiptFile -Encoding UTF8
    }
    Remove-UploadedBuild $Attempt
}

try {
    if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitProcess) {
        throw 'Run this script in 64-bit PowerShell on Windows (5.1 or newer).'
    }
    # Preserve caller-relative tool/identity paths when the build changes cwd.
    if ($Butler -match '[/\\]') { $Butler = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Butler) }
    if ($ButlerIdentity) { $ButlerIdentity = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ButlerIdentity) }
    if ($Check -and $InitAssets) { throw 'Use -Check or -InitAssets separately; -Check does not change asset checkouts.' }
    Invoke-Native 'python.exe' @((Join-Path $PSScriptRoot 'stamp-build-version.py'), '--check-clean')
    if ($UploadOnly) {
        if ($NoUpload -or $InitAssets) { throw '-UploadOnly cannot be combined with -NoUpload or -InitAssets.' }
        $runDirectory = (Resolve-Path -LiteralPath $UploadOnly).ProviderPath
        $attempts = @()
        if (Test-Path -LiteralPath (Join-Path $runDirectory 'receipt.json')) { $attempts = @($runDirectory) }
        else {
            foreach ($platform in $platforms) {
                $candidate = Join-Path $runDirectory $platform.ToLowerInvariant()
                $receiptPath = Join-Path $candidate 'receipt.json'
                if (Test-Path -LiteralPath $receiptPath) {
                    $state = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
                    if ($state.status -in 'packaged', 'upload_failed') { $attempts += $candidate }
                }
            }
        }
        if (-not $attempts.Count) { throw 'No pending packaged builds found. Pass a platform attempt directory to re-upload an already uploaded build.' }
        Get-Command 'python.exe' -CommandType Application -ErrorAction Stop | Out-Null
        foreach ($attempt in $attempts) { Invoke-AttemptUpload $attempt -ValidateOnly }
        $attemptPlatforms = @(foreach ($attempt in $attempts) {
            $platform = (Get-Content -LiteralPath (Join-Path $attempt 'receipt.json') -Raw | ConvertFrom-Json).platform
            if ($platform) { $platform } else { 'Windows' }
        })
        if (@($attemptPlatforms | Where-Object { $_ -ne 'Server' }).Count) { Assert-Butler }
        if ($attemptPlatforms -contains 'Server') { Assert-DockerServer -Upload }
        if ($Check) { Write-Host 'DESKTOP_UPLOAD_PREFLIGHT_PASS'; exit 0 }
        $failed = $false
        foreach ($attempt in $attempts) {
            try { Invoke-AttemptUpload $attempt } catch { Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red; $failed = $true }
        }
        if ($failed) { exit 1 }
        # A whole-run retry can now remove the shared workspace and summary too.
        if (Test-Path -LiteralPath (Join-Path $runDirectory 'summary.json')) {
            Remove-UploadedBuild $runDirectory -CompletedRetry
        } elseif (Test-Path -LiteralPath (Join-Path (Split-Path $runDirectory -Parent) 'summary.json')) {
            Remove-UploadedBuild (Split-Path $runDirectory -Parent) -CompletedRetry
        }
        exit 0
    }
    if (-not (Test-Path -LiteralPath $uproject)) { throw "Project missing: $uproject" }
    if (-not $OutputRoot) { $OutputRoot = Join-Path (Split-Path $projectRoot -Parent) 'builds\desktop' }
    $OutputRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputRoot)
    if ($OutputRoot -eq $projectRoot -or $OutputRoot.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Choose -OutputRoot outside the game repository.'
    }
    Write-Host "Project: $projectRoot"
    Write-Host "Platforms: $($platforms -join ', ') | Configuration: $Configuration | Output: $OutputRoot"
    $clientPlatforms = @($platforms | Where-Object { $_ -ne 'Server' })
    $buildServer = $platforms -contains 'Server'
    if ($buildServer) { Write-Host 'Server: Linux x64 Development dedicated server, Docker image' }
    if (-not $NoUpload -and $clientPlatforms.Count) {
        Write-Host 'Upload after packaging: aaggeeaa/respawn-and-rage (one channel per platform)'
        Test-Dependency 'Butler and itch.io authentication' { Assert-Butler }
    }
    if ($buildServer) {
        $dockerCheck = 'Docker Desktop (Linux containers, buildx)'
        if (-not $NoUpload) { $dockerCheck += " and Docker Hub access ($DockerCredentials)" }
        Test-Dependency $dockerCheck { Assert-DockerServer -Upload:(-not $NoUpload) }
    }

    Test-Dependency 'Git and Git LFS' {
        Invoke-Native 'git.exe' @('--version')
        Invoke-Native 'git.exe' @('lfs', 'version')
        Invoke-Native 'git.exe' @('-C', $projectRoot, 'rev-parse', '--verify', 'HEAD')
    }
    Test-Dependency 'Python 3.9+ on PATH as python' {
        # Target.cs also calls python, so a working py.exe alone is insufficient.
        Invoke-Native 'python.exe' @('-c', 'import sys; print(sys.version); sys.exit(0 if sys.version_info >= (3,9) else 1)')
    }
    Test-Dependency 'Unreal Engine 5.8.3 Installed Build' {
        $script:resolvedEngine = Find-Engine
        Write-Host "Engine found: $resolvedEngine"
        $version = Get-Content -LiteralPath (Join-Path $resolvedEngine 'Engine\Build\Build.version') -Raw | ConvertFrom-Json
        if ("$($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)" -ne '5.8.3') {
            throw 'This project requires exactly UE 5.8.3.'
        }
        foreach ($file in @('Engine\Build\InstalledBuild.txt', 'Engine\Build\BatchFiles\Build.bat',
                'Engine\Build\BatchFiles\RunUAT.bat', 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe',
                'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll')) {
            if (-not (Test-Path -LiteralPath (Join-Path $resolvedEngine $file))) { throw "Missing $file. Verify the engine in Epic Launcher (Windows support required)." }
        }
        $dotnet = @(Get-ChildItem (Join-Path $resolvedEngine 'Engine\Binaries\ThirdParty\DotNet\*\win-x64\dotnet.exe') -ErrorAction SilentlyContinue)
        if (-not $dotnet.Count) { throw 'Bundled Windows .NET runtime missing. Verify the engine in Epic Launcher.' }
        Invoke-Native $dotnet[0].FullName @('--info')
        Write-Host "Engine: $resolvedEngine"
    }
    if ($resolvedEngine) {
        Test-Dependency 'MSVC x64 and ATL (UE minimum and banned compiler versions)' { Assert-MSVC $resolvedEngine }
        if ($platforms -contains 'Windows') {
            Test-Dependency 'UE 5.8 Windows redistributables for packaging' { Assert-WindowsPrerequisites $resolvedEngine }
        }
    }
    Test-Dependency 'Windows SDK 10.0.22621+' {
        $kits = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue
        $sdkRoot = $kits.KitsRoot10
        if (-not $sdkRoot) { $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10' }
        $versions = @(Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory -ErrorAction SilentlyContinue | Where-Object {
            $_.Name -match '^10\.0\.\d+\.0$' -and [version]$_.Name -ge [version]'10.0.22621.0'
        })
        $usable = @(foreach ($version in $versions) {
            $sdkVersion = $version.Name
            $required = @("Include\$sdkVersion\um\Windows.h", "Include\$sdkVersion\ucrt\stdio.h",
                "Lib\$sdkVersion\um\x64\kernel32.lib", "Lib\$sdkVersion\ucrt\x64\ucrt.lib",
                "bin\$sdkVersion\x64\rc.exe", "bin\$sdkVersion\x64\mt.exe")
            if (-not @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $sdkRoot $_)) }).Count) { $sdkVersion }
        })
        if (-not $usable.Count) { throw 'Install a complete Windows SDK 10.0.22621 or newer using Visual Studio Installer.' }
        Write-Host "Windows SDK: $($usable -join ', ')"
    }
    if ($resolvedEngine) {
        if ($platforms -contains 'Linux') {
            Test-Dependency 'Unreal Linux target support' { Assert-LinuxSupport $resolvedEngine }
        }
        if ($platforms -contains 'Linux' -or $buildServer) {
            Test-Dependency 'Linux v26 toolchain (clang 20.1.8 and C++20 link)' { Assert-LinuxToolchain $resolvedEngine }
        }
        if ($buildServer) {
            Test-Dependency 'UE 5.8.3 with Linux dedicated server support' {
                $script:resolvedServerEngine = Find-ServerEngine $resolvedEngine
                Write-Host "Server engine: $resolvedServerEngine"
            }
        }
        Test-Dependency 'Enabled plugins, including PCGEx 0.79.2' {
            Assert-EnabledPlugins $resolvedEngine $projectRoot
        }
    }
    Test-Dependency 'Unreal Editor closed' {
        if (Get-Process -Name 'UnrealEditor', 'UnrealEditor-Cmd' -ErrorAction SilentlyContinue) { throw 'Close Unreal Editor and other cooks before building or updating assets.' }
    }
    Test-Dependency 'Disk space (10 GiB source / 60 GiB workspace minimum)' {
        Assert-FreeSpace $projectRoot 10
        Assert-FreeSpace $OutputRoot 60
    }
    Test-Dependency 'Runtime branding and font files' {
        foreach ($file in @('Branding\background.jpg', 'Branding\icon.png', 'Branding\Credits.txt',
                'Branding\Fonts\Teko\Fonts\Teko-SemiBold.ttf', 'Branding\Fonts\Teko\ThirdPartyLicenses\Teko-OFL.txt',
                'Branding\Fonts\Teko\CREDITS.txt')) {
            if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $file))) { throw "Missing $file. Restore the complete core checkout." }
        }
    }
    Test-Dependency 'Private assets and Content mounts (automatic junction setup)' {
        Invoke-AssetSetup -SetupScript (Join-Path $PSScriptRoot 'setup-assets.py') -ReadOnly:$Check `
            -FetchAssets:$InitAssets -CanPrepare ($problems.Count -eq 0)
    }
    if ($problems.Count) {
        throw ("Preflight failed. Fix these items and rerun:`n - " + ($problems -join "`n - ") +
            "`nExisting assets are mounted automatically on a normal build after tool checks pass; -Check only validates mounts." +
            "`nFor missing submodules/LFS, authenticate GitHub for all three private repositories and rerun with -InitAssets." +
            "`nFor Windows only, pass -TargetPlatform Windows; this does not require the Linux toolchain." +
            "`nWithout the dedicated server (no server engine or Docker), pass -TargetPlatform Windows,Linux.")
    }
    Write-Host 'DESKTOP_PREFLIGHT_PASS' -ForegroundColor Green
    if ($Check) { exit 0 }

    $runDirectory = Join-Path $OutputRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path $runDirectory | Out-Null
    $workspace = Join-Path $runDirectory 'project'
    $summaryFile = Join-Path $runDirectory 'summary.json'
    # Editor binaries are engine specific: a different server engine cooks from its own workspace.
    $separateServer = $buildServer -and
        [IO.Path]::GetFullPath($resolvedServerEngine).TrimEnd('\', '/') -ne [IO.Path]::GetFullPath($resolvedEngine).TrimEnd('\', '/')
    $summary = [ordered]@{
        status = 'running'; configuration = $Configuration; engine = $resolvedEngine; source = $uproject
        started = (Get-Date).ToString('o'); platforms = $platforms; workspace = $workspace
    }
    if ($buildServer) { $summary['server_engine'] = $resolvedServerEngine }
    $logFile = Join-Path $runDirectory 'editor.log'
    Write-Host "Build workspace: $runDirectory"
    try {
        Invoke-Native 'python.exe' @((Join-Path $PSScriptRoot 'prepare-desktop-build.py'), '--engine', $resolvedEngine, '--destination', $workspace)
        $buildProject = Join-Path $workspace 'RespawnAndRage.uproject'
        if ($clientPlatforms.Count -or ($buildServer -and -not $separateServer)) {
            Push-Location $workspace
            try { Invoke-EditorBuild $resolvedEngine $buildProject } finally { Pop-Location }
        }
        $failed = $false
        foreach ($platform in $platforms) {
            $attempt = Join-Path $runDirectory $platform.ToLowerInvariant()
            New-Item -ItemType Directory -Path $attempt | Out-Null
            $archive = Join-Path $attempt 'dist'
            $logFile = Join-Path $attempt 'build.log'
            $receiptFile = Join-Path $attempt 'receipt.json'
            $receipt = [ordered]@{
                status = 'running'; platform = $platform; configuration = $Configuration; engine = $resolvedEngine
                project = $buildProject; commit = (Get-Content -LiteralPath (Join-Path $workspace 'Build\SourceCommit.txt') -Raw).Trim()
                started = (Get-Date).ToString('o'); archive = $archive; log = $logFile
            }
            if ($platform -ne 'Server') { $receipt['itch_target'] = Get-ItchTarget $platform }
            try {
                if ($platform -eq 'Server') {
                    $serverWorkspace = $workspace
                    if ($separateServer) {
                        # Fab plugins are discovered in the client engine and copied as source.
                        $serverWorkspace = Join-Path $runDirectory 'server-project'
                        Invoke-Native 'python.exe' @((Join-Path $PSScriptRoot 'prepare-desktop-build.py'), '--engine', $resolvedEngine, '--destination', $serverWorkspace)
                    }
                    $serverProject = Join-Path $serverWorkspace 'RespawnAndRage.uproject'
                    $receipt.configuration = 'Development'
                    $receipt.engine = $resolvedServerEngine
                    $receipt.project = $serverProject
                    Push-Location $serverWorkspace
                    try {
                        if ($separateServer) { Invoke-EditorBuild $resolvedServerEngine $serverProject }
                        Invoke-ServerPackageBuild $resolvedServerEngine $serverProject $archive
                    } finally { Pop-Location }
                    $expectedVersion = (Get-Content -LiteralPath (Join-Path $serverWorkspace 'Build\GameVersion.txt') -Raw).Trim()
                    Invoke-Native 'python.exe' @($serverImageScript, 'stage', '--project', $projectRoot, '--archive', $archive,
                        '--target-receipt', (Join-Path $serverWorkspace 'Binaries\Linux\RespawnAndRageServer.target'),
                        '--version', $expectedVersion)
                    $receipt['docker_image'] = "$(Get-DockerRepository):$expectedVersion"
                    # Built now so a Docker failure is a package failure; the upload rebuilds from cache and pushes.
                    Invoke-Native 'python.exe' (@($serverImageScript, 'build', '--archive', $archive, '--version', $expectedVersion,
                        '--commit', $receipt.commit) + @(Get-DockerArguments))
                } else {
                    Push-Location $workspace
                    try { Invoke-PackageBuild $resolvedEngine $buildProject $archive $Configuration $platform } finally { Pop-Location }
                    if ($platform -eq 'Linux') {
                        foreach ($launcher in @(Get-ChildItem -LiteralPath $archive -Recurse -File -Filter '*.sh')) {
                            $text = (Get-Content -LiteralPath $launcher.FullName -Raw).Replace("`r`n", "`n")
                            [IO.File]::WriteAllText($launcher.FullName, $text, (New-Object Text.UTF8Encoding $false))
                        }
                    }
                    $expectedVersion = (Get-Content -LiteralPath (Join-Path $workspace 'Build\GameVersion.txt') -Raw).Trim()
                    $null = Get-GamePackage $archive $expectedVersion $platform
                }
                $receipt.status = 'packaged'
                $receipt['game_version'] = $expectedVersion
                Write-Host "${platform}_PACKAGE_PASS: $archive" -ForegroundColor Green
            } catch {
                $receipt.status = 'failed'
                $receipt['error'] = $_.Exception.Message
                Write-Host "ERROR ($platform): $($_.Exception.Message)" -ForegroundColor Red
                $failed = $true
            } finally {
                $receipt['finished'] = (Get-Date).ToString('o')
                $receipt | ConvertTo-Json | Set-Content -LiteralPath $receiptFile -Encoding UTF8
            }
        }
        if (-not $NoUpload) {
            foreach ($platform in $platforms) {
                $attempt = Join-Path $runDirectory $platform.ToLowerInvariant()
                $state = Get-Content -LiteralPath (Join-Path $attempt 'receipt.json') -Raw | ConvertFrom-Json
                if ($state.status -eq 'packaged') {
                    try { Invoke-AttemptUpload $attempt } catch {
                        Write-Host "ERROR ($platform upload): $($_.Exception.Message). Retry with -UploadOnly '$attempt'." -ForegroundColor Red
                        $failed = $true
                    }
                }
            }
        }
        if ($failed) { throw "One or more builds/uploads failed. Completed packages are retained in $runDirectory." }
        $summary.status = 'uploaded'
        if ($NoUpload) { $summary.status = 'packaged' }
        Write-Host "DESKTOP_BUILD_PASS: $runDirectory" -ForegroundColor Green
    } catch {
        $summary.status = 'failed'
        $summary['error'] = $_.Exception.Message
        throw
    } finally {
        $summary['finished'] = (Get-Date).ToString('o')
        $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryFile -Encoding UTF8
    }
    if ($summary.status -eq 'uploaded') { Remove-UploadedBuild $runDirectory }
} catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    if ($logFile) { Write-Host "Build log: $logFile" }
    exit 1
} finally {
    $env:LINUX_MULTIARCH_ROOT = $previousLinuxRoot
}
