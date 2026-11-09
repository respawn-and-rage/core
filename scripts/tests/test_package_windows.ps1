#requires -Version 5.1
# Dependency-free helper tests; runs on Windows PowerShell or cross-platform pwsh.
$ErrorActionPreference = 'Stop'
$scriptFile = Join-Path (Split-Path $PSScriptRoot -Parent) 'package-windows.ps1'
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($scriptFile, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
foreach ($definition in $ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $false)) {
    . ([scriptblock]::Create($definition.Extent.Text))
}

function Assert-True($Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-Throws([scriptblock]$Action, [string]$Pattern) {
    $message = $null
    try { & $Action } catch { $message = $_.Exception.Message }
    Assert-True ($message -and $message -match $Pattern) "Expected failure matching '$Pattern', received '$message'"
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) ('RR PowerShell tests ' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
$savedProgramFiles = $env:ProgramFiles
$savedProgramFilesX86 = ${env:ProgramFiles(x86)}
$savedProgramData = $env:ProgramData
$savedUserProfile = $env:USERPROFILE
$savedButlerKey = $env:BUTLER_API_KEY
$savedLinuxRoot = $env:LINUX_MULTIARCH_ROOT
$Engine = $null
$logFile = $null
$addedCDrive = $false
try {
    $shell = (Get-Process -Id $PID).Path
    Invoke-Native $shell @('-NoProfile', '-Command', 'exit 0')
    $captured = Invoke-Native $shell @('-NoProfile', '-Command', 'Write-Output "captured compiler version"') -Capture
    Assert-True ($captured -eq 'captured compiler version') 'Native version capture must preserve stdout'
    function Get-Command {
        param($Name, $CommandType, $ErrorAction)
        if ($Name -eq 'multiple-python.exe') {
            [pscustomobject]@{ Source = $shell }
            [pscustomobject]@{ Source = 'rr-missing-second-python.exe' }
            [pscustomobject]@{ Source = 'rr-missing-third-python.exe' }
        } else { Microsoft.PowerShell.Core\Get-Command @PSBoundParameters }
    }
    try {
        $captured = Invoke-Native 'multiple-python.exe' @('-NoProfile', '-Command', 'Write-Output "first PATH executable"') -Capture
        Assert-True ($captured -eq 'first PATH executable') 'Multiple applications must execute only the first PATH match'
    } finally { Remove-Item Function:Get-Command }
    Assert-Throws { Invoke-Native 'rr-intentionally-missing-executable-041d.exe' @() } 'not recognized|not found'
    Assert-Throws { Invoke-Native $shell @('-NoProfile', '-Command', '[Console]::Error.WriteLine("compiler failure"); exit 23') } 'exit 23'
    # Stderr alone must not fail Windows PowerShell 5.1 when the exit code is 0.
    $logFile = Join-Path $temporary 'build output.log'
    Invoke-Native $shell @('-NoProfile', '-Command', '[Console]::Error.WriteLine("diagnostic on stderr"); Write-Output "normal output"; exit 0')
    $log = Get-Content -LiteralPath $logFile -Raw
    Assert-True ($log -match 'diagnostic on stderr' -and $log -match 'normal output') 'Both native streams must be logged'
    $captured = Invoke-Native $shell @('-NoProfile', '-Command', '[Console]::Error.WriteLine("captured stderr"); Write-Output "captured stdout"; exit 0') -Capture
    Assert-True ($captured -match 'captured stderr' -and $captured -match 'captured stdout') 'Capture must preserve both streams without failing on stderr alone'
    Assert-True ($captured -notmatch 'CategoryInfo|FullyQualifiedErrorId') 'Captured messages must exclude PowerShell ErrorRecord formatting'
    Assert-Throws { Invoke-Native $shell @('-NoProfile', '-Command', '[Console]::Error.WriteLine("missing asset mount"); exit 1') -Capture } 'exit 1'
    $logFile = $null
    # Arguments containing spaces must survive native invocation unchanged.
    $argumentProbe = Join-Path $temporary 'argument probe.ps1'
    Set-Content -LiteralPath $argumentProbe -Value 'if ($args.Count -ne 1 -or $args[0] -ne "project with spaces") { exit 19 }; exit 0'
    Invoke-Native $shell @('-NoProfile', '-File', $argumentProbe, 'project with spaces')

    $problems = New-Object 'System.Collections.Generic.List[string]'
    Test-Dependency 'missing engine' { throw 'install engine' }
    Test-Dependency 'missing compiler' { throw 'install compiler' }
    Test-Dependency 'working dependency' { }
    Assert-True ($problems.Count -eq 2) 'Preflight must collect independent failures'
    Assert-True ($problems[1] -match 'missing compiler: install compiler') 'Preflight must retain corrective messages'

    # Test actual Launcher JSON discovery with the registry absent. Windows-only
    # locations are mapped into a temporary tree; no real registry is touched.
    function Get-ItemProperty { param($Path, $ErrorAction) return $null }
    if (-not (Get-PSDrive C -ErrorAction SilentlyContinue)) {
        New-PSDrive -Name C -PSProvider FileSystem -Root $temporary | Out-Null
        $addedCDrive = $true
    }
    $env:ProgramFiles = Join-Path $temporary 'Program Files'
    $env:ProgramData = Join-Path $temporary 'ProgramData'
    $preferred = $env:UE_ENGINE_ROOT
    $env:UE_ENGINE_ROOT = $null
    $engineFolder = Join-Path $temporary 'Custom Epic location\UE_5.8'
    $buildFolder = Join-Path $engineFolder 'Engine\Build'
    New-Item -ItemType Directory -Path $buildFolder -Force | Out-Null
    $manifestFolder = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher'
    New-Item -ItemType Directory -Path $manifestFolder -Force | Out-Null
    @{ InstallationList = @(@{ AppName = 'UE_5.8'; InstallLocation = $engineFolder }) } |
        ConvertTo-Json -Depth 4 | Set-Content (Join-Path $manifestFolder 'LauncherInstalled.dat')
    @{ MajorVersion = 5; MinorVersion = 8; PatchVersion = 3 } | ConvertTo-Json |
        Set-Content (Join-Path $buildFolder 'Build.version')
    # On Unix C: is not mounted. Missing Windows default paths are intentionally
    # ignored just as absent directories are on Windows.
    $found = Find-Engine
    Assert-True ($found -eq [IO.Path]::GetFullPath($engineFolder)) 'Discover custom engine from Launcher manifest'
    $Engine = $engineFolder
    Assert-True ((Find-Engine) -eq $found) 'Explicit engine path must override discovery'
    $Engine = $null
    $env:UE_ENGINE_ROOT = $engineFolder
    Assert-True ((Find-Engine) -eq $found) 'Discover engine from UE_ENGINE_ROOT'
    Assert-Throws { Assert-FreeSpace $temporary ([double]::MaxValue) } 'Need at least'
    Assert-FreeSpace (Join-Path $temporary 'not-yet-created\output') 0

    # UE 5.8 stages two VC redistributables, not the old UEPrereqSetup bundle.
    $redist = Join-Path $engineFolder 'Engine\Extras\Redist\en-us'
    New-Item -ItemType Directory -Path $redist -Force | Out-Null
    Assert-Throws { Assert-WindowsPrerequisites $engineFolder } 'vc_redist.x64.exe'
    Set-Content (Join-Path $redist 'vc_redist.x64.exe') 'fixture'
    Assert-Throws { Assert-WindowsPrerequisites $engineFolder } 'vc_redist.arm64.exe'
    Set-Content (Join-Path $redist 'vc_redist.arm64.exe') 'fixture'
    Assert-WindowsPrerequisites $engineFolder

    # A Marketplace regex used to overwrite $matches and produce a null Path.
    $pluginProject = Join-Path $temporary 'Plugin project'
    $marketplace = Join-Path $engineFolder 'Engine\Plugins\Marketplace\PCGEx'
    New-Item -ItemType Directory -Path (Join-Path $marketplace 'Source'), $pluginProject -Force | Out-Null
    @{ Plugins = @(@{ Name = 'PCGExtendedToolkit'; Enabled = $true }) } | ConvertTo-Json -Depth 5 |
        Set-Content (Join-Path $pluginProject 'RespawnAndRage.uproject')
    $pluginFile = Join-Path $marketplace 'PCGExtendedToolkit.uplugin'
    @{ VersionName = '0.79.2'; Modules = @(@{ Name = 'PCGEx' }); SupportedTargetPlatforms = @('Win64', 'Linux') } |
        ConvertTo-Json -Depth 5 | Set-Content $pluginFile
    $platforms = @('Windows', 'Linux')
    Assert-EnabledPlugins $engineFolder $pluginProject
    Remove-Item (Join-Path $marketplace 'Source')
    Assert-Throws { Assert-EnabledPlugins $engineFolder $pluginProject } 'Plugin source required'
    New-Item -ItemType Directory (Join-Path $marketplace 'Source') | Out-Null
    @{ VersionName = '0.79.1'; Modules = @(@{ Name = 'PCGEx' }) } | ConvertTo-Json -Depth 5 | Set-Content $pluginFile
    Assert-Throws { Assert-EnabledPlugins $engineFolder $pluginProject } '0.79.2 required'

    # Mock only external Unreal invocations to verify that an Editor failure
    # never starts UAT, and that a Game failure propagates to the caller.
    $invocations = New-Object 'System.Collections.Generic.List[object]'
    $failOnCall = 1
    function Invoke-Native([string]$FilePath, [string[]]$Arguments, [switch]$Capture) {
        $invocations.Add(@{ path = $FilePath; arguments = $Arguments })
        if ($invocations.Count -eq $failOnCall) { throw 'simulated compiler failure' }
        if ($FilePath -eq $fakeVswhere) { return $fakeVS }
        if ($Capture) { return $fakeCompilerVersion }
        if ($Arguments -contains '--target=x86_64-unknown-linux-gnu') {
            $outputIndex = [Array]::IndexOf($Arguments, '-o') + 1
            [IO.File]::WriteAllBytes($Arguments[$outputIndex], $elf)
        }
    }
    $projectFile = Join-Path $temporary 'Game source\RespawnAndRage.uproject'
    $archiveFolder = Join-Path $temporary 'Output folder\dist'
    Assert-Throws { Invoke-EditorBuild $engineFolder $projectFile } 'compiler failure'
    Assert-True ($invocations.Count -eq 1) 'A failed Editor build must stop before UAT'
    $invocations.Clear()
    $failOnCall = 1
    Assert-Throws { Invoke-PackageBuild $engineFolder $projectFile $archiveFolder 'Shipping' } 'compiler failure'
    Assert-True ($invocations.Count -eq 1) 'A failed UAT invocation must propagate'
    $invocations.Clear()
    $failOnCall = 0
    Invoke-EditorBuild $engineFolder $projectFile
    Invoke-PackageBuild $engineFolder $projectFile $archiveFolder 'Shipping' 'Windows'
    Invoke-PackageBuild $engineFolder $projectFile $archiveFolder 'Shipping' 'Linux'
    Assert-True ($invocations.Count -eq 3) 'Compile Editor once and package both platforms'
    Assert-True ($invocations[0].arguments[2] -eq 'Development') 'Editor configuration must remain Development'
    Assert-True ($invocations[1].arguments -contains '-clientconfig=Shipping') 'Game configuration must honor Shipping'
    Assert-True ($invocations[1].arguments -contains "-archivedirectory=$archiveFolder") 'Archive paths with spaces must remain one argument'
    Assert-True ($invocations[1].arguments -contains '-platform=Win64' -and $invocations[1].arguments -contains '-prereqs') 'Windows build must stage Windows prerequisites'
    Assert-True ($invocations[2].arguments -contains '-platform=Linux' -and $invocations[2].arguments -contains '-clientarchitecture=x64') 'Linux build must target x64'
    Assert-True (-not ($invocations[2].arguments -contains '-prereqs')) 'Linux package must not stage Windows prerequisites'
    $invocations.Clear()
    $serverEngine = Join-Path $temporary 'Source engine'
    Invoke-ServerPackageBuild $serverEngine $projectFile $archiveFolder
    $serverArguments = $invocations[0].arguments
    Assert-True ($invocations[0].path -eq (Join-Path $serverEngine 'Engine\Build\BatchFiles\RunUAT.bat')) 'Server must cook with the server engine'
    Assert-True ($serverArguments -contains '-target=RespawnAndRageServer' -and $serverArguments -contains '-server' -and
        $serverArguments -contains '-noclient' -and $serverArguments -contains '-serverconfig=Development' -and
        $serverArguments -contains '-serverplatform=Linux' -and $serverArguments -contains '-serverarchitecture=x64') 'Server must build Linux x64 Development only'
    Assert-True (-not ($serverArguments -match '^-map=')) 'Server must use MapsToCook'
    Assert-True (-not ($serverArguments -contains '-installed')) 'A source engine must not be treated as installed'
    New-Item -ItemType Directory -Path (Join-Path $serverEngine 'Engine\Build') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $serverEngine 'Engine\Build\InstalledBuild.txt') -Value ''
    $invocations.Clear()
    Invoke-ServerPackageBuild $serverEngine $projectFile $archiveFolder
    Assert-True ($invocations[0].arguments -contains '-installed') 'An Installed Build with server support needs -installed'
    $DockerCredentials = Join-Path $temporary 'secrets\dockerhub.json'
    $DockerRepository = $null
    $NoUpload = $true
    Assert-True ((Get-DockerRepository) -eq 'local/respawn-and-rage-server') 'Local-only images need no credentials'
    $NoUpload = $false
    Assert-Throws { Get-DockerRepository } 'credentials missing'
    New-Item -ItemType Directory -Path (Split-Path $DockerCredentials -Parent) -Force | Out-Null
    @{ username = 'rr'; token = 'dckr_pat_never_logged'; repository = 'rr/respawn-and-rage-server' } | ConvertTo-Json |
        Set-Content -LiteralPath $DockerCredentials
    Assert-True ((Get-DockerRepository) -eq 'rr/respawn-and-rage-server') 'Repository comes from the credentials file'
    $dockerArguments = @(Get-DockerArguments -Upload)
    Assert-True ($dockerArguments -contains $DockerCredentials -and -not ($dockerArguments -contains 'dckr_pat_never_logged')) 'The token is passed only as a file path'
    $DockerRepository = 'rr/override'
    Assert-True ((Get-DockerRepository) -eq 'rr/override') '-DockerRepository overrides the file'

    # Server engine discovery: explicit choices never fall back; otherwise the
    # client engine first, then UnrealVersionSelector registrations.
    $savedServerRoot = $env:UE_SERVER_ENGINE_ROOT
    $env:UE_SERVER_ENGINE_ROOT = $null
    $launcherEngine = Join-Path $temporary 'Launcher UE_5.8'
    $sourceEngine = Join-Path $temporary 'Source UE_5.8'
    $oldEngine = Join-Path $temporary 'Source UE_5.7'
    foreach ($item in @(@($launcherEngine, 3), @($sourceEngine, 3), @($oldEngine, 2))) {
        New-Item -ItemType Directory -Path (Join-Path $item[0] 'Engine\Build') -Force | Out-Null
        @{ MajorVersion = 5; MinorVersion = 8; PatchVersion = $item[1] } | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $item[0] 'Engine\Build\Build.version')
    }
    $serverCapable = @()
    $checkedEngines = New-Object 'System.Collections.Generic.List[string]'
    function Test-ServerEngine([string]$EngineRoot) { $checkedEngines.Add($EngineRoot); return $serverCapable -contains $EngineRoot }
    function Get-ItemProperty { param($Path, $ErrorAction)
        if ($Path -like '*Unreal Engine\Builds') {
            [pscustomobject]@{ '{OLD-GUID}' = $oldEngine; '{SOURCE-GUID}' = ($sourceEngine + '\'); PSPath = 'registry' }
        }
    }
    try {
        $ServerEngine = $null
        Assert-Throws { Find-ServerEngine $launcherEngine } 'UE_SERVER_ENGINE_ROOT'
        Assert-True (-not ($checkedEngines -contains $oldEngine)) 'Other engine versions must not be checked'
        $serverCapable = @($sourceEngine)
        $checkedEngines.Clear()
        Assert-True ((Find-ServerEngine $launcherEngine) -eq $sourceEngine) 'Registered source engine must be found'
        Assert-True ($checkedEngines[0] -eq $launcherEngine) 'The client engine is tried first'
        $serverCapable = @($launcherEngine, $sourceEngine)
        Assert-True ((Find-ServerEngine $launcherEngine) -eq $launcherEngine) 'A server-capable client engine avoids a second workspace'
        $serverCapable = @($sourceEngine)
        $env:UE_SERVER_ENGINE_ROOT = $launcherEngine
        Assert-Throws { Find-ServerEngine $launcherEngine } 'cannot build'
        $env:UE_SERVER_ENGINE_ROOT = $sourceEngine
        Assert-True ((Find-ServerEngine $launcherEngine) -eq $sourceEngine) 'UE_SERVER_ENGINE_ROOT is honored'
        $ServerEngine = $oldEngine
        $checkedEngines.Clear()
        Assert-Throws { Find-ServerEngine $launcherEngine } 'cannot build'
        Assert-True ($checkedEngines.Count -eq 1) '-ServerEngine overrides the environment and never falls back'
        $ServerEngine = $null
    } finally {
        $env:UE_SERVER_ENGINE_ROOT = $savedServerRoot
        Remove-Item Function:Test-ServerEngine
        function Get-ItemProperty { param($Path, $ErrorAction) return $null }
    }

    $assetSetup = Join-Path $temporary 'Repo with spaces\scripts\setup-assets.py'
    $invocations.Clear()
    Invoke-AssetSetup $assetSetup
    Assert-True ($invocations.Count -eq 1 -and $invocations[0].path -eq 'python.exe') 'Normal builds must invoke asset setup automatically'
    Assert-True ($invocations[0].arguments.Count -eq 1 -and $invocations[0].arguments[0] -eq $assetSetup) 'Mount downloaded assets without fetching or splitting script paths'
    $invocations.Clear()
    Invoke-AssetSetup $assetSetup -ReadOnly
    Assert-True ($invocations[0].arguments.Count -eq 2 -and $invocations[0].arguments[1] -eq '--check') 'Check-only must never create junctions'
    $invocations.Clear()
    Invoke-AssetSetup $assetSetup -FetchAssets
    Assert-True ($invocations[0].arguments[1] -eq '--init') 'Explicit initialization must fetch assets and mount them'
    $invocations.Clear()
    Invoke-AssetSetup $assetSetup -FetchAssets -CanPrepare $false
    Assert-True ($invocations[0].arguments[1] -eq '--check') 'Failed tool checks must not fetch assets or change mounts'
    $invocations.Clear()
    $failOnCall = 1
    Assert-Throws { Invoke-AssetSetup $assetSetup } 'compiler failure'
    Assert-True ($invocations.Count -eq 1) 'Conflicting/missing assets must fail asset setup before building'
    $failOnCall = 0

    # Discover VS 2022 as well as 2026 and use the engine's compiler policy.
    ${env:ProgramFiles(x86)} = Join-Path $temporary 'Program Files x86'
    $fakeVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    New-Item -ItemType Directory (Split-Path $fakeVswhere -Parent) -Force | Out-Null
    Set-Content $fakeVswhere 'fixture'
    $fakeVS = Join-Path $temporary 'Visual Studio 2022'
    $compilerRoot = Join-Path $fakeVS 'VC\Tools\MSVC'
    $windowsSdk = Join-Path $engineFolder 'Engine\Config\Windows\Windows_SDK.json'
    New-Item -ItemType Directory (Split-Path $windowsSdk -Parent) -Force | Out-Null
    @{ MinimumVisualCppVersion = '14.38.33130'; BannedVisualCppVersions = @(
        '14.50.0-14.50.35722', '14.44.0-14.44.35210', '14.40.0-14.43.99999', '14.39.0-14.39.99999') } |
        ConvertTo-Json | Set-Content $windowsSdk
    $failOnCall = 0
    $compilerFolder = Join-Path $compilerRoot '14.44.35211'
    foreach ($file in @('bin\Hostx64\x64\cl.exe', 'bin\Hostx64\x64\link.exe',
                       'include\vector', 'lib\x64\libcmt.lib', 'atlmfc\include\atlbase.h', 'atlmfc\lib\x64\atls.lib')) {
        $path = Join-Path $compilerFolder $file
        New-Item -ItemType Directory (Split-Path $path -Parent) -Force | Out-Null
        Set-Content $path 'fixture'
    }
    $invocations.Clear()
    Assert-MSVC $engineFolder
    Assert-True ($invocations[0].arguments -contains '[17.8,19.0)') 'Discover supported VS 2022 and 2026 families'
    function Get-Item {
        param($LiteralPath)
        [pscustomobject]@{ VersionInfo = [pscustomobject]@{
            ProductMajorPart = 14; ProductMinorPart = 44; ProductBuildPart = 35217 } }
    }
    try {
        $version = Get-MSVCVersion ([IO.DirectoryInfo]$compilerFolder)
        Assert-True ($version -eq [version]'14.44.35217') 'Serviced compiler product version must override its older folder family'
    } finally { Remove-Item Function:Get-Item }
    foreach ($version in @('14.50.35722', '14.44.35210', '14.38.33129')) {
        $badFolder = Join-Path $compilerRoot $version
        Move-Item $compilerFolder $badFolder
        Assert-Throws { Assert-MSVC $engineFolder } 'No compatible MSVC'
        Move-Item $badFolder $compilerFolder
    }
    $newCompiler = Join-Path $compilerRoot '14.50.35723'
    Move-Item $compilerFolder $newCompiler
    Assert-MSVC $engineFolder
    Remove-Item (Join-Path $newCompiler 'atlmfc\lib\x64\atls.lib')
    Assert-Throws { Assert-MSVC $engineFolder } 'No compatible MSVC'
    ${env:ProgramFiles(x86)} = $savedProgramFilesX86

    $elf = New-Object byte[] 20
    $elf[0] = 127; $elf[1] = 69; $elf[2] = 76; $elf[3] = 70; $elf[4] = 2; $elf[5] = 1; $elf[16] = 3; $elf[18] = 62
    $toolchain = Join-Path $temporary 'Linux toolchain with spaces'
    $archRoot = Join-Path $toolchain 'x86_64-unknown-linux-gnu'
    foreach ($file in @('bin\clang++.exe', 'bin\ld.lld.exe', 'bin\llvm-ar.exe', 'bin\llvm-objcopy.exe',
            'include\c++\v1\vector', 'lib64\libc++.a', 'lib64\libc++abi.a')) {
        $path = Join-Path $archRoot $file
        New-Item -ItemType Directory -Path (Split-Path $path -Parent) -Force | Out-Null
        Set-Content -LiteralPath $path -Value 'fixture'
    }
    $sdkFolder = Join-Path $engineFolder 'Engine\Config\Linux'
    New-Item -ItemType Directory -Path $sdkFolder -Force | Out-Null
    @{ MainVersion = 'v26_clang-20.1.8-rockylinux8' } | ConvertTo-Json | Set-Content (Join-Path $sdkFolder 'Linux_SDK.json')
    $LinuxToolchain = $toolchain
    $marker = Join-Path $toolchain 'ToolchainVersion.txt'
    Set-Content -LiteralPath $marker -Value 'v25_clang-18.1.0-rockylinux8'
    Assert-Throws { Assert-LinuxToolchain $engineFolder } 'Install Epic v26'
    Set-Content -LiteralPath $marker -Value 'v26_clang-20.1.8-rockylinux8'
    $fakeCompilerVersion = 'clang version 19.0.0'
    Assert-Throws { Assert-LinuxToolchain $engineFolder } 'must run clang 20.1.8'
    $fakeCompilerVersion = 'clang version 20.1.8 (Epic)'
    $invocations.Clear()
    Assert-LinuxToolchain $engineFolder
    Assert-True ($env:LINUX_MULTIARCH_ROOT -eq $toolchain) 'Validated toolchain must be the one passed to UBT'
    Assert-True ($invocations.Count -eq 2) 'Toolchain preflight must run compiler version and C++ link'
    $invocations.Clear()
    $failOnCall = 2
    Assert-Throws { Assert-LinuxToolchain $engineFolder } 'compiler failure'
    $failOnCall = 0
    Remove-Item -LiteralPath (Join-Path $archRoot 'lib64\libc++abi.a')
    Assert-Throws { Assert-LinuxToolchain $engineFolder } 'Incomplete Linux toolchain'
    $Configuration = 'Development'
    $engineWin64 = Join-Path $engineFolder 'Engine\Binaries\Win64'
    $engineLinux = Join-Path $engineFolder 'Engine\Binaries\Linux'
    New-Item -ItemType Directory -Path $engineWin64, $engineLinux -Force | Out-Null
    Assert-Throws { Assert-LinuxSupport $engineFolder } 'enable Linux target support'
    Set-Content -LiteralPath (Join-Path $engineWin64 'UnrealEditor-LinuxTargetPlatform.dll') -Value 'fixture'
    Set-Content -LiteralPath (Join-Path $engineLinux 'UnrealGame.target') -Value 'fixture'
    Assert-LinuxSupport $engineFolder
    $Configuration = 'Shipping'
    Assert-Throws { Assert-LinuxSupport $engineFolder } 'enable Linux target support'
    Set-Content -LiteralPath (Join-Path $engineLinux 'UnrealGame-Linux-Shipping.target') -Value 'fixture'
    Assert-LinuxSupport $engineFolder

    $Butler = 'mock-butler.exe'
    $ButlerIdentity = $null
    $itchTarget = 'aaggeeaa/respawn-and-rage:windows-x86_64'
    $env:BUTLER_API_KEY = $null
    $env:USERPROFILE = $temporary
    $invocations.Clear()
    Assert-Throws { Assert-Butler } 'Run butler login first'
    Assert-True ($invocations.Count -eq 1) 'Missing credentials must stop before an API call or push'
    $env:BUTLER_API_KEY = 'test-only-not-a-credential'
    $invocations.Clear()
    Assert-Butler
    Assert-True ($invocations.Count -eq 2 -and $invocations[1].arguments[0] -eq 'status') 'Auth preflight must use read-only status'
    Assert-True (-not ($invocations[1].arguments -contains $env:BUTLER_API_KEY)) 'API key must never enter command arguments'
    $invocations.Clear()
    $failOnCall = 2
    Assert-Throws { Assert-Butler } 'could not access'
    Assert-True ($invocations.Count -eq 2) 'Failed auth preflight must not push'
    $failOnCall = 0
    $env:BUTLER_API_KEY = $null
    $ButlerIdentity = Join-Path $temporary 'private identity file'
    Set-Content -LiteralPath $ButlerIdentity -Value 'test-only-not-a-credential'
    $invocations.Clear()
    Assert-Butler
    Assert-True ($invocations[1].arguments[0] -eq '--identity' -and $invocations[1].arguments[1] -eq $ButlerIdentity) 'Explicit identity path must survive spaces'

    $staged = Join-Path $archiveFolder 'Windows'
    $paks = Join-Path $staged 'RespawnAndRage\Content\Paks'
    $stampFolder = Join-Path $staged 'RespawnAndRage\Build'
    $runtimeFolder = Join-Path $staged 'RespawnAndRage\Binaries\Win64'
    New-Item -ItemType Directory -Path $paks, $stampFolder, $runtimeFolder -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $staged 'RespawnAndRage.exe') -Value 'fixture'
    Set-Content -LiteralPath (Join-Path $runtimeFolder 'RespawnAndRage.exe') -Value 'fixture'
    Set-Content -LiteralPath (Join-Path $paks 'RespawnAndRage-Windows.utoc') -Value 'fixture'
    $stampFile = Join-Path $stampFolder 'GameVersion.txt'
    Set-Content -LiteralPath $stampFile -Value '26.10.20.abcdef12'
    $invocations.Clear()
    Invoke-ItchUpload $archiveFolder '26.10.20.abcdef12'
    Assert-True ($invocations.Count -eq 1) 'Upload must run exactly one push'
    $pushed = $invocations[0].arguments
    Assert-True ($pushed -contains $staged) 'Push only the staged game root, excluding sibling logs and receipts'
    Assert-True (-not ($pushed -contains $archiveFolder)) 'Do not push the parent archive directory'
    Assert-True ($pushed -contains $itchTarget -and $pushed[-1] -eq '26.10.20.abcdef12') 'Push Windows channel with packaged version'
    $uploadReceiptFile = Join-Path (Split-Path $archiveFolder -Parent) 'itch-upload.json'
    $uploadReceipt = Get-Content -LiteralPath $uploadReceiptFile -Raw | ConvertFrom-Json
    Assert-True ($uploadReceipt.status -eq 'uploaded') 'Successful push must persist upload status'
    $invocations.Clear()
    $failOnCall = 1
    Assert-Throws { Invoke-ItchUpload $archiveFolder '26.10.20.abcdef12' } 'compiler failure'
    $uploadReceipt = Get-Content -LiteralPath $uploadReceiptFile -Raw | ConvertFrom-Json
    Assert-True ($uploadReceipt.status -eq 'failed' -and (Test-Path -LiteralPath $stampFile)) 'Upload failure must be recorded without removing the package'
    $failOnCall = 0
    $invocations.Clear()
    Assert-Throws { Invoke-ItchUpload $archiveFolder '26.10.19.abcdef12' } 'version differs'
    Set-Content -LiteralPath $stampFile -Value 'invalid version'
    Assert-Throws { Invoke-ItchUpload $archiveFolder $null } 'Invalid packaged game version'
    Assert-True ($invocations.Count -eq 0) 'Reject invalid packages before invoking Butler'
    Set-Content -LiteralPath $stampFile -Value '26.10.20.abcdef12'
    Remove-Item -LiteralPath (Join-Path $paks 'RespawnAndRage-Windows.utoc')
    Assert-Throws { Invoke-ItchUpload $archiveFolder $null } 'containers are missing'

    $linuxArchive = Join-Path $temporary 'linux attempt\dist'
    $linuxStage = Join-Path $linuxArchive 'Linux'
    $linuxGame = Join-Path $linuxStage 'RespawnAndRage'
    foreach ($folder in @('Content\Paks', 'Build', 'Binaries\Linux')) { New-Item -ItemType Directory -Path (Join-Path $linuxGame $folder) -Force | Out-Null }
    [IO.File]::WriteAllText((Join-Path $linuxStage 'RespawnAndRage.sh'), "#!/bin/sh`nexit 0`n")
    Set-Content -LiteralPath (Join-Path $linuxGame 'Build\GameVersion.txt') -Value '26.10.20.abcdef12'
    Set-Content -LiteralPath (Join-Path $linuxGame 'Content\Paks\game.pak') -Value 'fixture'
    $linuxBinary = Join-Path $linuxGame 'Binaries\Linux\RespawnAndRage-Linux-Shipping'
    [IO.File]::WriteAllBytes($linuxBinary, $elf)
    $invocations.Clear()
    Invoke-ItchUpload $linuxArchive '26.10.20.abcdef12' 'Linux'
    Assert-True ($invocations[0].arguments -contains 'aaggeeaa/respawn-and-rage:linux-x86_64') 'Linux must upload to its own channel'
    Assert-True ($invocations[0].arguments -contains '--fix-permissions') 'Linux upload must restore executable permissions on Windows'
    Assert-True ($invocations[0].arguments -contains $linuxStage) 'Linux upload must use the staged root'
    $elf[18] = 183
    [IO.File]::WriteAllBytes($linuxBinary, $elf)
    Assert-Throws { Invoke-ItchUpload $linuxArchive $null 'Linux' } 'Linux x86_64 ELF'
    $elf[18] = 62
    [IO.File]::WriteAllBytes($linuxBinary, $elf)
    [IO.File]::WriteAllText((Join-Path $linuxStage 'RespawnAndRage.sh'), "#!/bin/sh`r`nexit 0`r`n")
    Assert-Throws { Invoke-ItchUpload $linuxArchive $null 'Linux' } 'LF line endings'
    # Upload retries clean only after a successful push and receipt write.
    $retry = Join-Path $temporary 'retry attempt'
    $retryArchive = Join-Path $retry 'dist'
    New-Item -ItemType Directory -Path $retry -Force | Out-Null
    # Reuse the known-valid Windows package after restoring its container.
    Set-Content -LiteralPath (Join-Path $paks 'RespawnAndRage-Windows.utoc') -Value 'fixture'
    Copy-Item -LiteralPath $archiveFolder -Destination $retryArchive -Recurse
    $retryReceipt = Join-Path $retry 'receipt.json'
    @{status='packaged'; platform='Windows'; game_version='26.10.20.abcdef12'; finished=''} |
        ConvertTo-Json | Set-Content -LiteralPath $retryReceipt
    $invocations.Clear()
    Invoke-AttemptUpload $retry -ValidateOnly
    Assert-True ($invocations.Count -eq 0 -and (Test-Path $retryReceipt)) 'Validation must not upload or clean'
    $failOnCall = 1
    Assert-Throws { Invoke-AttemptUpload $retry } 'compiler failure'
    Assert-True ($invocations.Count -eq 1) 'Failed upload must not invoke cleanup'
    Assert-True ((Get-Content $retryReceipt -Raw | ConvertFrom-Json).status -eq 'upload_failed') 'Failed retry must retain its state'
    $failOnCall = 0
    $invocations.Clear()
    Invoke-AttemptUpload $retry
    Assert-True ($invocations.Count -eq 2) 'Successful retry must push then clean'
    Assert-True ($invocations[1].path -eq 'python.exe' -and $invocations[1].arguments -contains '--uploaded-run' -and $invocations[1].arguments -contains $retry) 'Cleanup must target exactly the uploaded attempt'
    Assert-True ((Get-Content $retryReceipt -Raw | ConvertFrom-Json).status -eq 'uploaded') 'Cleanup starts after the success receipt is written'
    Assert-True ($null -eq $script:logFile) 'Cleanup must not write back into deleted logs'
    Write-Host 'WINDOWS_HELPER_TESTS_PASS'
} finally {
    $env:ProgramFiles = $savedProgramFiles
    ${env:ProgramFiles(x86)} = $savedProgramFilesX86
    $env:ProgramData = $savedProgramData
    $env:USERPROFILE = $savedUserProfile
    $env:BUTLER_API_KEY = $savedButlerKey
    $env:LINUX_MULTIARCH_ROOT = $savedLinuxRoot
    if (Test-Path variable:preferred) { $env:UE_ENGINE_ROOT = $preferred }
    if ($addedCDrive) { Remove-PSDrive C }
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
