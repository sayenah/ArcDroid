# R3d-4 only, using an installed Windows frontend and authored inputs.
# SPDX-License-Identifier: GPL-2.0-only
#requires -Version 7.0
param(
    [Parameter(Mandatory = $true)][string]$InputDirectory,
    [Parameter(Mandatory = $true)][string]$OrdinaryRouteBuild,
    [Parameter(Mandatory = $true)][string]$RetroArchDirectory,
    [Parameter(Mandatory = $true)][string]$WorkDirectory
)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell 7 on Windows' }
$InputDirectory = (Resolve-Path -LiteralPath $InputDirectory).Path
$OrdinaryRouteBuild = (Resolve-Path -LiteralPath $OrdinaryRouteBuild).Path
$RetroArchDirectory = (Resolve-Path -LiteralPath $RetroArchDirectory).Path
$WorkDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($WorkDirectory)
if (Test-Path -LiteralPath $WorkDirectory) { throw 'WorkDirectory must be new (first-use evidence requires fresh stores)' }
Get-Command python -ErrorAction Stop | Out-Null
& python -c 'import sys; raise SystemExit(0 if sys.version_info >= (3,8) else 1)'
if ($LASTEXITCODE -ne 0) { throw 'Python 3.8 or newer must be on PATH for the switch classifier' }
foreach ($name in @('arcdroid_libretro.dll', 'arcdroid_libretro.info', 'arcdroid-test.rom', 'arcdroid-test.adf')) {
    if (-not (Test-Path -LiteralPath (Join-Path $InputDirectory $name) -PathType Leaf)) { throw "Missing CI input: $name" }
}
foreach ($name in @('arcdroid_libretro.dll', 'arcdroid_rpcroute_libretro.dll', 'arcdroid_rpcroute_libretro.info', 'generated/rpc-route/route.jfd', 'generated/rpc-route/system')) {
    if (-not (Test-Path -LiteralPath (Join-Path $OrdinaryRouteBuild $name))) { throw "Missing gate-open authored input: $name" }
}
if (-not (Test-Path -LiteralPath (Join-Path $RetroArchDirectory 'retroarch.exe'))) { throw 'RetroArchDirectory must contain retroarch.exe' }

# Import only these function definitions from the CI driver. Do not execute its
# download, legacy/R2c/F4 legs, setup statements or any other top-level code.
$helperNames = @(
    'ConfigPath', 'Send-QuitPair', 'Move-ItemWithSharingRetry',
    'Get-EventDataMap', 'Get-EventField', 'Write-RetroArchFailureEvidence',
    'Write-MediaRunFailureEvidence', 'Wait-RetroArchStartReady',
    'Start-MediaRun', 'Send-MediaCommand', 'Stop-MediaRun', 'Get-RunLogTail',
    'Capture-RawScreenshot', 'Capture-ExpectedVisualSignature', 'Capture-StableRawScreenshot'
)
$tokens = $null; $parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'test-retroarch-windows.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
foreach ($name in $helperNames) {
    $definitions = @($ast.FindAll({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name
    }, $true))
    if ($definitions.Count -ne 1) { throw "Expected one CI helper: $name" }
    . ([scriptblock]::Create($definitions[0].Extent.Text))
}

New-Item -ItemType Directory $WorkDirectory | Out-Null
$evidenceDirectory = Join-Path $WorkDirectory 'evidence'
$runtimeDirectory = Join-Path $WorkDirectory 'frontend'
$coreDirectory = Join-Path $WorkDirectory 'cores'
$infoDirectory = Join-Path $WorkDirectory 'info'
New-Item -ItemType Directory $evidenceDirectory, $runtimeDirectory, $coreDirectory, $infoDirectory | Out-Null
# Copy application binaries/assets only. Never copy or run against the user's
# config, cores, saves or system directory. All frontend writes stay in Work.
Copy-Item -LiteralPath (Join-Path $RetroArchDirectory 'retroarch.exe') $runtimeDirectory
Get-ChildItem -LiteralPath $RetroArchDirectory -Filter '*.dll' -File | Copy-Item -Destination $runtimeDirectory
if (Test-Path -LiteralPath (Join-Path $RetroArchDirectory 'assets')) {
    Copy-Item -Recurse -LiteralPath (Join-Path $RetroArchDirectory 'assets') $runtimeDirectory
}
Copy-Item -LiteralPath (Join-Path $InputDirectory 'arcdroid_libretro.info') $infoDirectory
$retroArch = Join-Path $runtimeDirectory 'retroarch.exe'
$retroArchVersion = '1.22.2'
$ciRetryAttempts = 3
$script:RetroArchStartedProcesses = [System.Collections.Generic.List[object]]::new()
Add-Type -AssemblyName System.Drawing
$oldAudio = $env:SDL_AUDIODRIVER
$env:SDL_AUDIODRIVER = 'dummy'
Start-Transcript -Path (Join-Path $WorkDirectory 'ordinary-transcript.txt') | Out-Null
try {
    # GUI-subsystem executables must be explicitly waited, even for --version.
    $versionOut = Join-Path $evidenceDirectory 'retroarch-version.txt'
    $versionErr = Join-Path $evidenceDirectory 'retroarch-version.stderr.txt'
    $versionProcess = Start-Process $retroArch -ArgumentList '--version' -PassThru -NoNewWindow `
        -RedirectStandardOutput $versionOut -RedirectStandardError $versionErr
    if (-not $versionProcess.WaitForExit(10000)) { Stop-Process -Id $versionProcess.Id -Force; throw 'RetroArch --version timed out' }
    $versionProcess.WaitForExit()
    if ($versionProcess.ExitCode -ne 0 -or (Get-Content -Raw $versionOut) -notmatch '(?<![0-9.])1\.22\.2(?![0-9.])') { throw 'This evidence run requires RetroArch 1.22.2; see version logs' }
    . (Join-Path $PSScriptRoot 'test-retroarch-rpc-route.ps1')
    if (-not (Test-Path -LiteralPath (Join-Path $evidenceDirectory 'rpc-ordinary-contention.txt'))) { throw 'Missing contention evidence' }
    Get-Content (Join-Path $evidenceDirectory 'rpc-ordinary-contention.txt') | Write-Host
    Get-Content (Join-Path $evidenceDirectory 'rpc-ordinary-switch.txt') | Write-Host
    Write-Host "R3d-4 ONLY PASS: RetroArch $retroArchVersion; closed legacy, contended first use, archive-free reopen, secondary checks, switch capability, fresh Arculator, J4 and shipping J3. Evidence: $evidenceDirectory"
} finally {
    foreach ($process in $script:RetroArchStartedProcesses) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() }
    }
    $env:SDL_AUDIODRIVER = $oldAudio
    Stop-Transcript | Out-Null
}
