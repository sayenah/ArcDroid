# R3d-4 authored ordinary-route evidence. Dot-sourced with the pinned RA helpers.
# SPDX-License-Identifier: GPL-2.0-only
$ordinaryBuild = (Resolve-Path $OrdinaryRouteBuild).Path
$ordinaryFixtures = Join-Path $ordinaryBuild 'generated/rpc-route'
$ordinaryRoot = Join-Path $WorkDirectory 'ordinary-route'
New-Item -ItemType Directory $ordinaryRoot | Out-Null
# R3d-3 R-10 requires an absolute native lock root. RetroArch passes a
# relative savefile_directory through unchanged; resolve before deriving it.
$ordinaryRoot = (Resolve-Path -LiteralPath $ordinaryRoot).Path
$ordinarySystem = Join-Path $ordinaryRoot 'system'
Copy-Item -Recurse (Join-Path $ordinaryFixtures 'system') $ordinarySystem
$ordinarySave = Join-Path $ordinaryRoot 'save'
New-Item -ItemType Directory $ordinarySave | Out-Null
$ordinaryContent = (Resolve-Path (Join-Path $ordinaryFixtures 'route.jfd')).Path
$ordinaryCore = Join-Path $coreDirectory 'arcdroid_rpcroute_libretro.dll'
Copy-Item (Join-Path $ordinaryBuild 'arcdroid_rpcroute_libretro.dll') $ordinaryCore
Copy-Item (Join-Path $ordinaryBuild 'arcdroid_rpcroute_libretro.info') $infoDirectory
$ordinaryCheats = Join-Path $ordinaryRoot 'cheats'
$ordinaryCheatCore = Join-Path $ordinaryCheats 'ArcDroid Ordinary Route Test'
New-Item -ItemType Directory -Force $ordinaryCheatCore | Out-Null
@'
cheats = "1"
cheat0_desc = "Authored no-op"
cheat0_code = "00000000"
cheat0_enable = "false"
cheat0_handler = "0"
'@ | Set-Content -Encoding ASCII (Join-Path $ordinaryCheatCore 'route.cht')
$ordinaryConfig = Join-Path $ordinaryRoot 'retroarch.cfg'
$ordinaryOptions = Join-Path $ordinaryRoot 'options.cfg'
@'
arcdroid_machine = "Auto"
arcdroid_cpu = "Auto"
arcdroid_os = "Auto"
'@ | Set-Content -Encoding ASCII $ordinaryOptions
# Match the working native legs: CI exposes Microsoft's OpenGL 1.1 renderer.
# RA's gl driver saves a fallback config and exits(1) on that renderer; it does
# not continue with the fallback. GDI also preserves the raw screenshot path.
@"
video_driver = "gdi"
audio_driver = "sdl2"
audio_enable = "true"
record_driver = "wav"
midi_driver = "null"
midi_input = "Null"
midi_output = "Null"
input_driver = "dinput"
video_fullscreen = "false"
video_windowed_fullscreen = "false"
video_gpu_screenshot = "false"
video_vsync = "false"
pause_nonactive = "false"
network_cmd_enable = "true"
network_cmd_port = "55355"
quit_press_twice = "false"
cheat_database_path = "$(ConfigPath $ordinaryCheats)"
core_options_path = "$(ConfigPath $ordinaryOptions)"
global_core_options = "true"
game_specific_options = "false"
auto_overrides_enable = "false"
config_save_on_exit = "false"
system_directory = "$(ConfigPath $ordinarySystem)"
savefile_directory = "$(ConfigPath $ordinarySave)"
savestate_directory = "$(ConfigPath $ordinarySave)"
sort_savefiles_enable = "false"
sort_savefiles_by_content_enable = "false"
libretro_directory = "$(ConfigPath $coreDirectory)"
libretro_info_path = "$(ConfigPath $infoDirectory)"
screenshot_directory = "$(ConfigPath $evidenceDirectory)"
log_verbosity = "true"
frontend_log_level = "0"
log_to_file_timestamp = "false"
libretro_log_level = "1"
user_language = "0"
run_ahead_enabled = "true"
run_ahead_frames = "1"
run_ahead_secondary_instance = "true"
rewind_enable = "true"
"@ | Set-Content -Encoding ASCII $ordinaryConfig
$core = $ordinaryCore
# In RA 1.22.2, core_options_path is only the global fallback unless
# global_core_options=true; game/folder options have still higher priority.
# Each process gets a new config/options pair. RA may save that options file
# at unload, but no later process reads it or a prior core's saved .opt.
function New-OrdinaryLegConfig([string]$BaseConfig, [string]$Name) {
    $directory = Join-Path $ordinaryRoot "options-$Name"
    New-Item -ItemType Directory $directory | Out-Null
    $legOptions = Join-Path $directory 'options.cfg'
    Copy-Item -LiteralPath $ordinaryOptions $legOptions
    $legConfig = Join-Path $directory 'retroarch.cfg'
    Get-Content -LiteralPath $BaseConfig | Where-Object {
        $_ -notmatch '^core_options_path\s*='
    } | Set-Content -Encoding ASCII $legConfig
    'core_options_path = "' + (ConfigPath $legOptions) + '"' | Add-Content -Encoding ASCII $legConfig
    return (Resolve-Path -LiteralPath $legConfig).Path
}
function Assert-OrdinaryOptions([string]$Log, [string]$Name) {
    # These frontend DEBUG lines are emitted on the core's GET_VARIABLE calls,
    # proving delivered values rather than merely inspecting our input file.
    & python -B (Join-Path $PSScriptRoot 'check-retroarch-rpc-options.py') $Log |
        Tee-Object -FilePath (Join-Path $evidenceDirectory "$Name-options.txt")
    if ($LASTEXITCODE -ne 0) { throw "$Name received unexpected core options" }
}
function Wait-OrdinaryLog($Run, [string]$Pattern, [int]$AfterLines = 0) {
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        if (Test-Path $Run.RetroLog) {
            $text = [string](Get-Content -Raw $Run.RetroLog)
            if ($AfterLines) { $text = (@(Get-Content $Run.RetroLog | Select-Object -Skip $AfterLines) -join "`n") }
            if ($text -match 'summary: phase=pending .*state=11 ') { throw "Ordinary-route preparation failed: $(Get-RunLogTail $Run)" }
            if ($text -match $Pattern) { return $text }
            if ($text -match 'Failed to load content') { throw 'Real RetroArch primary ordinary load failed before the expected event' }
        }
        if ($Run.Process.HasExited) { throw 'Ordinary-route process exited before its expected event' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Ordinary-route event timed out: $Pattern"
}
# GET_STATUS is supported by the pinned frontend, including while paused.
# Wait for its reply before issuing commands whose evidence depends on pause;
# a fixed delay does not establish that no live run-ahead iteration can race.
function Wait-OrdinaryPlayback($Run, [string]$State) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Send-MediaCommand $Run 'GET_STATUS'
        try {
            $remote = [Net.IPEndPoint]::new([Net.IPAddress]::Any, 0)
            $reply = [Text.Encoding]::ASCII.GetString($Run.Udp.Receive([ref]$remote))
            if ($reply -match ('^GET_STATUS ' + $State + ' ')) { return }
        } catch [Net.Sockets.SocketException] { }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Ordinary-route frontend did not reach $State within 10 seconds: $(Get-RunLogTail $Run)"
}
function Invoke-OrdinaryPreemptRefusal($Run) {
    $boundary = @(Get-Content $Run.RetroLog).Count
    Send-MediaCommand $Run 'PREEMPT_TOGGLE'
    [void](Wait-OrdinaryLog $Run '(?m)^\[WARN\] \[Run-Ahead Preemptive\] Preemptive Frames has been disabled because this core does not support save states\.\r?$' $boundary)
}
function Get-OrdinarySecondaryCount($Run) {
    if (-not (Test-Path $Run.RetroLog)) { return 0 }
    $pattern = [regex]::Escape('secondary load refused before preparation, guest execution and writable stores.')
    return [regex]::Matches([string](Get-Content -Raw $Run.RetroLog), $pattern).Count
}
function Wait-OrdinarySecondaryCount($Run, [int]$Minimum) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        if ((Get-OrdinarySecondaryCount $Run) -ge $Minimum) { return }
        if ($Run.Process.HasExited) { throw 'Primary exited during secondary refusal evidence' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Frontend did not exercise the expected additional secondary load'
}
# Closed shipping still boots the flagged authored carrier on Arculator.
$closedSystem = Join-Path $ordinaryRoot 'closed-system'
$closedSave = Join-Path $ordinaryRoot 'closed-save'
New-Item -ItemType Directory -Force (Join-Path $closedSystem 'arcdroid/firmware'), $closedSave | Out-Null
Copy-Item (Join-Path $InputDirectory 'arcdroid-test.rom') (Join-Path $closedSystem 'arcdroid/firmware/control.rom')
$closedConfig = Join-Path $ordinaryRoot 'closed.cfg'
Get-Content $ordinaryConfig | Where-Object { $_ -notmatch '^(system_directory|savefile_directory|savestate_directory|run_ahead_enabled|rewind_enable)' } | Set-Content -Encoding ASCII $closedConfig
@"
system_directory = "$(ConfigPath $closedSystem)"
savefile_directory = "$(ConfigPath $closedSave)"
savestate_directory = "$(ConfigPath $closedSave)"
run_ahead_enabled = "false"
rewind_enable = "false"
"@ | Add-Content -Encoding ASCII $closedConfig
$config = '"' + (New-OrdinaryLegConfig $closedConfig 'rpc-ordinary-closed-legacy') + '"'
$core = (Resolve-Path (Join-Path $InputDirectory 'arcdroid_libretro.dll')).Path
$run = $null
try {
    $run = Start-MediaRun ('"' + $ordinaryContent + '"') 'rpc-ordinary-closed-legacy'
    Assert-OrdinaryOptions $run.RetroLog $run.Name
    [void](Wait-OrdinaryLog $run 'Firmware: ArcDroid test ROM')
    $text = [string](Get-Content -Raw $run.RetroLog)
    if ($text -notmatch 'which ArcDroid does not emulate; booting anyway' -or (Test-Path (Join-Path $closedSave 'arcdroid/hd-riscpc'))) { throw 'Closed shipping did not preserve the legacy route/notice' }
} finally { if ($run) { Stop-MediaRun $run } }
Assert-OrdinaryOptions $run.RetroLog $run.Name
$core = $ordinaryCore
$ordinarySwitchDrivable = $true
foreach ($leg in @(1, 2)) {
    $run = $null
    $pendingOwner = $null
    if ($leg -eq 1) {
        # Hold a real native defaults lock: pending screenshots are deterministic,
        # without a test flag or a timing-dependent race against fast preparation.
        $lockDirectory = Join-Path $ordinarySave 'arcdroid/locks-riscpc'
        New-Item -ItemType Directory -Force $lockDirectory | Out-Null
        $key = (Get-FileHash -Algorithm SHA256 (Join-Path $ordinarySystem 'arcdroid/firmware/ro371.rom')).Hash.ToLowerInvariant()
        $storeHeader = Get-Content -Raw (Join-Path $PSScriptRoot '../libretro/rpc_cmos_store.h')
        $procedure = [regex]::Match($storeHeader, 'ARC_RPC_CMOS_PROCEDURE_REVISION ([0-9]+)u').Groups[1].Value
        $timing = [regex]::Match($storeHeader, 'ARC_RPC_CMOS_TIMING_REVISION ([0-9]+)u').Groups[1].Value
        if (-not $procedure -or -not $timing) { throw 'Cannot identify defaults lock revision' }
        $pendingOwner = [IO.File]::Open((Join-Path $lockDirectory "d-$key-m3-p$procedure-t$timing.lock"), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::ReadWrite)
        $pendingOwner.Lock(0, 1)
    }
    try {
        $config = '"' + (New-OrdinaryLegConfig $ordinaryConfig "rpc-ordinary-$leg") + '"'
        $run = Start-MediaRun ('"' + $ordinaryContent + '"') "rpc-ordinary-$leg"
        Assert-OrdinaryOptions $run.RetroLog $run.Name
        if ($leg -eq 1) {
            [void](Wait-OrdinaryLog $run 'N-7: Preparing RiscPC settings for first use')
            if ((Get-Content -Raw $run.RetroLog) -match 'summary: phase=pending .*state=10 ') { throw 'Contention did not hold preparation pending' }
            $pending = Capture-RawScreenshot $run 'rpc-ordinary-pending'
            if ($pending.Colours -ne 1 -or $pending.BackgroundPixel -ne '000000') { throw 'Pending core raster was not black' }
            $pendingOwner.Dispose()
            $pendingOwner = $null
        }
        $text = Wait-OrdinaryLog $run 'summary: phase=pending .*state=10 '
        if ($text -notmatch 'ArcDroid build:.*gate=open' -or
            $text -match 'Failed to load content') { throw 'Gate-open primary was refused or the wrong core was loaded' }
        $summary = [regex]::Match($text, 'summary: phase=pending .*state=10 [^\r\n]*').Value
        if ($leg -eq 1) {
            # Namespace mkdir steps also count as wait frames. First use needs
            # two successful native acquisitions (content/defaults); a genuine
            # held defaults lock must cause additional non-namespace attempts.
            $attempts = [regex]::Match($summary, 'lock_attempts=([0-9]+)').Groups[1].Value
            $waits = [regex]::Match($summary, 'lock_wait_frames=([0-9]+)').Groups[1].Value
            if (-not $attempts -or [UInt64]$attempts -le 2 -or -not $waits -or [UInt64]$waits -eq 0) { throw 'Defaults lock did not produce genuine pending contention' }
            "defaults-contention: native_attempts=$attempts wait_frames=$waits; pending black frame and RUNNING after release PASS" |
                Set-Content -Encoding ASCII (Join-Path $evidenceDirectory 'rpc-ordinary-contention.txt')
        }
        if ($summary.Length -gt 3900 -or $summary -notmatch 'first_pending_ms=[0-9]+' -or $summary -notmatch 'running_ms=[0-9]+' -or $summary -notmatch 'desktop=unavailable') { throw 'Ordinary-route summary lacks bounded phase evidence' }
        if ($text -notmatch 'catalogue=authored' -or $text -notmatch 'production-observer-cells=0') { throw 'Authored/production qualification identity mismatch' }
        [void](Wait-OrdinaryLog $run 'status first-field:')
        # Route ROM: guest IIC calendar read, then a 4096-byte 8bpp ring,
        # first 2048 bytes 0x55, second 2048 bytes 0xaa, at 640x256.
        # First-field is not a readiness promise. Poll actual decoded pixels
        # (bounded by the shared 12 s helper), never wait a CI-tuned delay.
        # SHA-256 is over all opaque BGRA pixels in row order, not PNG bytes.
        $expectedRaster = @{ Dimensions = '640x256'; VisualSignature = '1aad5e3f49d9b588396b52bc7902234a90432207f2ef526db81af38a31895cff' }
        $guest = Capture-ExpectedVisualSignature $run "rpc-ordinary-guest-$leg" $expectedRaster
        if ($guest.Colours -lt 2) { throw 'No authored guest raster after START' }
        if ($leg -eq 1) {
            # Initial size-zero run-ahead fails; preempt deinit resets its
            # availability flags. Re-enter with serialization-size-known set.
            $beforeSecondary = Get-OrdinarySecondaryCount $run
            Invoke-OrdinaryPreemptRefusal $run
            Wait-OrdinarySecondaryCount $run ($beforeSecondary + 1)
            Send-MediaCommand $run 'PAUSE_TOGGLE'
            Wait-OrdinaryPlayback $run 'PAUSED'
            foreach ($toggle in @(1, 2)) {
                $beforeSecondary = Get-OrdinarySecondaryCount $run
                Invoke-OrdinaryPreemptRefusal $run
                Send-MediaCommand $run 'CHEAT_TOGGLE'
                Wait-OrdinarySecondaryCount $run ($beforeSecondary + 1)
                $cheatState = if ($toggle -eq 1) { 'ON' } else { 'OFF' }
                [void](Wait-OrdinaryLog $run ('\[Cheats\] Cheat: #0 \[' + $cheatState + '\]: Authored no-op'))
            }
            Send-MediaCommand $run 'PAUSE_TOGGLE'
            Wait-OrdinaryPlayback $run 'PLAYING'
            Send-MediaCommand $run 'REWIND'
            [void](Capture-ExpectedVisualSignature $run 'rpc-ordinary-after-secondary-refusals' $guest)
            $text = [string](Get-Content -Raw $run.RetroLog)
            if ($text -notmatch 'Load game-specific cheatfile:' -or $text -match 'N-1[458]') { throw 'Cheat path was not exercised or secondary damaged primary ownership' }
            if ([regex]::Matches($text, 'summary: phase=pending .*state=10 ').Count -ne 1) { throw 'A secondary reached RUNNING' }
        }
        if ($leg -eq 2) {
            # Fable: only the exact frontend rejection permits a not-drivable
            # result. Same-process R3d-4 evidence rests on Odin3 assisted J6
            # and the harness. Recognised commands keep the strict checks;
            # there is no relaunch fallback and no timeout/crash exception.
            Copy-Item (Join-Path $InputDirectory 'arcdroid-test.rom') (Join-Path $ordinarySystem 'arcdroid/firmware/authored-arculator.rom')
            $ordinaryArc = (Resolve-Path (Join-Path $InputDirectory 'arcdroid-test.adf')).Path
            $loadCommand = "LOAD_CONTENT $(ConfigPath $ordinaryCore)|$(ConfigPath $ordinaryArc)"
            $switchStart = @(Get-Content $run.RetroLog).Count
            Send-MediaCommand $run $loadCommand
            $rejection = '[WARN] Unrecognized command "' + $loadCommand + '" received.'
            [void](Wait-OrdinaryLog $run ('(?m)^' + [regex]::Escape($rejection) + '\r?$|Firmware: ArcDroid test ROM') $switchStart)
            $switchResult = & python -B (Join-Path $PSScriptRoot 'check-retroarch-rpc-switch.py') $run.RetroLog `
                --after-lines $switchStart --command $loadCommand --core ((Resolve-Path $ordinaryCore).Path)
            if ($LASTEXITCODE -ne 0) { throw 'Ordinary switch log classification failed' }
            if ($run.Process.HasExited) { throw 'LOAD_CONTENT relaunched instead of switching' }
            if ($switchResult -ceq 'unsupported') {
                $ordinarySwitchDrivable = $false
                [void](Capture-ExpectedVisualSignature $run 'rpc-ordinary-command-unsupported' $guest)
                $ordinarySwitchEvidence = "R3d-4 RetroArch ${retroArchVersion}: same-process Arculator switch is not drivable by this frontend build (exact LOAD_CONTENT rejection); evidence rests on Odin3 assisted J6 and the harness."
            } elseif ($switchResult -ceq 'strict') {
                [void](Wait-OrdinaryLog $run 'Firmware: ArcDroid test ROM' $switchStart)
                $after = Capture-StableRawScreenshot $run 'rpc-ordinary-after-arculator' 2
                $ordinaryArcSignature = $after.VisualSignature
                if ($run.Process.HasExited) { throw 'LOAD_CONTENT relaunched instead of switching' }
                $ordinarySwitchEvidence = "R3d-4 RetroArch ${retroArchVersion}: same-process Arculator switch observed; raw-pixel baseline captured."
            } else { throw 'Unknown ordinary switch classification' }
            Write-Host $ordinarySwitchEvidence
            $ordinarySwitchEvidence | Set-Content -Encoding ASCII (Join-Path $evidenceDirectory 'rpc-ordinary-switch.txt')
        }
    } finally {
        if ($pendingOwner) { $pendingOwner.Dispose() }
        if ($run) { Stop-MediaRun $run }
    }
    Assert-OrdinaryOptions $run.RetroLog $run.Name
    if ($leg -eq 2) {
        # Include late load/error lines through clean QUIT, not just the first
        # reply. A rejection cannot excuse a later core change or load failure.
        $finalSwitchResult = & python -B (Join-Path $PSScriptRoot 'check-retroarch-rpc-switch.py') $run.RetroLog `
            --after-lines $switchStart --command $loadCommand --core ((Resolve-Path $ordinaryCore).Path)
        if ($LASTEXITCODE -ne 0 -or $finalSwitchResult -cne $switchResult) { throw 'Ordinary switch outcome changed before QUIT' }
    }
    if ($leg -eq 1) {
        Remove-Item (Join-Path $ordinarySystem 'arcdroid/riscpc/boot/HardDisc4.zip'), (Join-Path $ordinarySystem 'arcdroid/riscpc/boot/PlingSystem.zip')
    }
}
# Fresh-process Arculator control with the same core/config; compare raw pixels.
# An exact frontend rejection leaves RPC running, so that path has no switched
# Arculator image to compare. Its fresh Arculator control must still render.
$run = $null
try {
    $config = '"' + (New-OrdinaryLegConfig $ordinaryConfig 'rpc-ordinary-fresh-arculator') + '"'
    $run = Start-MediaRun ('"' + $ordinaryArc + '"') 'rpc-ordinary-fresh-arculator'
    Assert-OrdinaryOptions $run.RetroLog $run.Name
    [void](Wait-OrdinaryLog $run 'Firmware: ArcDroid test ROM')
    $fresh = Capture-StableRawScreenshot $run 'rpc-ordinary-fresh-arculator' 2
    if ($fresh.Colours -lt 2) { throw 'Fresh Arculator control has no guest raster' }
    if ($ordinarySwitchDrivable -and $fresh.VisualSignature -ne $ordinaryArcSignature) { throw 'Arculator pixels differ after RPC switch' }
} finally { if ($run) { Stop-MediaRun $run } }
Assert-OrdinaryOptions $run.RetroLog $run.Name
function Invoke-OrdinaryRefusal([string]$Library, [string]$Name, [string]$Pattern) {
    $legConfig = '"' + (New-OrdinaryLegConfig $archiveConfig $Name) + '"'
    $refusalLog = Join-Path $evidenceDirectory "$Name.log"
    $text = ""
    $arguments = @("--config=$legConfig", "--libretro=$Library", '--verbose', "--log-file=$refusalLog", '--max-frames=180', ('"' + $ordinaryContent + '"'))
    Wait-RetroArchStartReady
    $refusal = Start-Process $retroArch -ArgumentList $arguments -PassThru -NoNewWindow
    $script:RetroArchStartedProcesses.Add($refusal)
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        do {
            if (Test-Path $refusalLog) {
                $text = [string](Get-Content -Raw $refusalLog)
                if ($text -match $Pattern) { break }
            }
            if ($refusal.HasExited) { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        Assert-OrdinaryOptions $refusalLog $Name
        if ($text -notmatch $Pattern -or $text -match 'summary: phase=pending .*state=10 ') { throw "$Name did not give its class-A refusal" }
    } finally {
        if (-not $refusal.HasExited) {
            $udp = [System.Net.Sockets.UdpClient]::new();$udp.Connect('127.0.0.1',55355)
            Send-QuitPair $udp;$udp.Dispose()
            if (-not $refusal.WaitForExit(5000)) { Stop-Process -Id $refusal.Id -Force;$refusal.WaitForExit() }
        }
    }
    Assert-OrdinaryOptions $refusalLog $Name
}
# J4 needs recognised authored firmware and a FRESH store. It is test-core evidence.
$archiveSave = Join-Path $ordinaryRoot 'missing-archives-save'
New-Item -ItemType Directory $archiveSave | Out-Null
$archiveConfig = Join-Path $ordinaryRoot 'missing-archives.cfg'
Get-Content $ordinaryConfig | Where-Object { $_ -notmatch '^(savefile_directory|savestate_directory)' } | Set-Content -Encoding ASCII $archiveConfig
@"
savefile_directory = "$(ConfigPath $archiveSave)"
savestate_directory = "$(ConfigPath $archiveSave)"
"@ | Add-Content -Encoding ASCII $archiveConfig
Invoke-OrdinaryRefusal $ordinaryCore 'rpc-ordinary-test-j4' 'N-6: First use.*HardDisc4.zip and PlingSystem.zip'
if (Test-Path (Join-Path $archiveSave 'arcdroid/hd-riscpc')) { throw 'Missing archives created a store' }
# J3 uses the unmodified gate-open SHIPPING DLL, with an empty firmware folder.
Get-ChildItem (Join-Path $ordinarySystem 'arcdroid/firmware') | Remove-Item
Invoke-OrdinaryRefusal (Join-Path $ordinaryBuild 'arcdroid_libretro.dll') 'rpc-ordinary-shipping-j3' 'N-1: Needs RiscPC'
'R3d-4 test-core: first use, archive-free reopen, primary run-ahead/rewind refusal and fresh Arculator control; switch outcome in rpc-ordinary-switch.txt; shipping J3. J4 is test-core evidence.' |
    Set-Content -Encoding ASCII (Join-Path $evidenceDirectory 'rpc-ordinary-result.txt')
