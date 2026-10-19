# R3d-4 authored ordinary-route evidence. Dot-sourced with the pinned RA helpers.
# SPDX-License-Identifier: GPL-2.0-only
$ordinaryBuild = (Resolve-Path $OrdinaryRouteBuild).Path
$ordinaryFixtures = Join-Path $ordinaryBuild 'generated/rpc-route'
$ordinaryRoot = Join-Path $WorkDirectory 'ordinary-route'
New-Item -ItemType Directory $ordinaryRoot | Out-Null
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
@"
video_driver = "gl"
audio_driver = "sdl2"
audio_enable = "true"
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
system_directory = "$(ConfigPath $ordinarySystem)"
savefile_directory = "$(ConfigPath $ordinarySave)"
savestate_directory = "$(ConfigPath $ordinarySave)"
libretro_directory = "$(ConfigPath $coreDirectory)"
libretro_info_path = "$(ConfigPath $infoDirectory)"
screenshot_directory = "$(ConfigPath $evidenceDirectory)"
log_verbosity = "true"
libretro_log_level = "1"
run_ahead_enabled = "true"
run_ahead_frames = "1"
run_ahead_secondary_instance = "true"
rewind_enable = "true"
"@ | Set-Content -Encoding ASCII $ordinaryConfig
$config = '"' + (Resolve-Path $ordinaryConfig).Path + '"'
$core = $ordinaryCore
function Wait-OrdinaryLog($Run, [string]$Pattern) {
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        if (Test-Path $Run.RetroLog) {
            $text = [string](Get-Content -Raw $Run.RetroLog)
            if ($text -match $Pattern) { return $text }
            if ($text -match 'Failed to load content') { throw 'Real RetroArch primary ordinary load failed before the expected event' }
        }
        if ($Run.Process.HasExited) { throw 'Ordinary-route process exited before its expected event' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Ordinary-route event timed out: $Pattern"
}
function Get-OrdinarySecondaryCount($Run) {
    if (-not (Test-Path $Run.RetroLog)) { return 0 }
    $pattern = [regex]::Escape('RPC secondary load refused before preparation, guest execution and writable stores.')
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
$config = '"' + (Resolve-Path $closedConfig).Path + '"'
$core = (Resolve-Path (Join-Path $InputDirectory 'arcdroid_libretro.dll')).Path
$run = $null
try {
    $run = Start-MediaRun ('"' + $ordinaryContent + '"') 'rpc-ordinary-closed-legacy'
    [void](Wait-OrdinaryLog $run 'Firmware: ArcDroid test ROM')
    $text = [string](Get-Content -Raw $run.RetroLog)
    if ($text -notmatch 'which ArcDroid does not emulate; booting anyway' -or (Test-Path (Join-Path $closedSave 'arcdroid/hd-riscpc'))) { throw 'Closed shipping did not preserve the legacy route/notice' }
} finally { if ($run) { Stop-MediaRun $run } }
$config = '"' + (Resolve-Path $ordinaryConfig).Path + '"'
$core = $ordinaryCore
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
        $run = Start-MediaRun ('"' + $ordinaryContent + '"') "rpc-ordinary-$leg"
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
        if ($summary.Length -gt 3900 -or $summary -notmatch 'first_pending_ms=[0-9]+' -or $summary -notmatch 'running_ms=[0-9]+' -or $summary -notmatch 'desktop=unavailable') { throw 'Ordinary-route summary lacks bounded phase evidence' }
        if ($text -notmatch 'catalogue=authored' -or $text -notmatch 'production-observer-cells=0') { throw 'Authored/production qualification identity mismatch' }
        [void](Wait-OrdinaryLog $run 'status first-field:')
        $guest = Capture-StableRawScreenshot $run "rpc-ordinary-guest-$leg"
        if ($guest.Colours -lt 2) { throw 'No authored guest raster after START' }
        if ($leg -eq 1) {
            # Initial size-zero run-ahead fails; preempt deinit resets its
            # availability flags. Re-enter with serialization-size-known set.
            $beforeSecondary = Get-OrdinarySecondaryCount $run
            Send-MediaCommand $run 'PREEMPT_TOGGLE'
            Wait-OrdinarySecondaryCount $run ($beforeSecondary + 1)
            Send-MediaCommand $run 'PAUSE_TOGGLE'
            Start-Sleep -Milliseconds 300
            foreach ($toggle in @(1, 2)) {
                $beforeSecondary = Get-OrdinarySecondaryCount $run
                Send-MediaCommand $run 'PREEMPT_TOGGLE'
                Start-Sleep -Milliseconds 300
                Send-MediaCommand $run 'CHEAT_TOGGLE'
                Wait-OrdinarySecondaryCount $run ($beforeSecondary + 1)
            }
            Send-MediaCommand $run 'PAUSE_TOGGLE'
            Send-MediaCommand $run 'REWIND'
            [void](Capture-ExpectedVisualSignature $run 'rpc-ordinary-after-secondary-refusals' $guest)
            $text = [string](Get-Content -Raw $run.RetroLog)
            if ($text -notmatch 'Load game-specific cheatfile:' -or $text -match 'N-1[458]') { throw 'Cheat path was not exercised or secondary damaged primary ownership' }
            if ([regex]::Matches($text, 'summary: phase=pending .*state=10 ').Count -ne 1) { throw 'A secondary reached RUNNING' }
        }
        if ($leg -eq 2) {
            # No relaunch fallback: this exact process must load Arculator.
            Copy-Item (Join-Path $InputDirectory 'arcdroid-test.rom') (Join-Path $ordinarySystem 'arcdroid/firmware/authored-arculator.rom')
            $ordinaryArc = (Resolve-Path (Join-Path $InputDirectory 'arcdroid-test.adf')).Path
            Send-MediaCommand $run "LOAD_CONTENT $(ConfigPath $ordinaryCore)|$(ConfigPath $ordinaryArc)"
            [void](Wait-OrdinaryLog $run 'Firmware: ArcDroid test ROM')
            $after = Capture-StableRawScreenshot $run 'rpc-ordinary-after-arculator'
            $ordinaryArcSignature = $after.VisualSignature
            if ($run.Process.HasExited) { throw 'LOAD_CONTENT relaunched instead of switching' }
        }
    } finally {
        if ($pendingOwner) { $pendingOwner.Dispose() }
        if ($run) { Stop-MediaRun $run }
    }
    if ($leg -eq 1) {
        Remove-Item (Join-Path $ordinarySystem 'arcdroid/riscpc/boot/HardDisc4.zip'), (Join-Path $ordinarySystem 'arcdroid/riscpc/boot/PlingSystem.zip')
    }
}
# Fresh-process Arculator control with the same core/config; compare raw pixels.
$run = $null
try {
    $run = Start-MediaRun ('"' + $ordinaryArc + '"') 'rpc-ordinary-fresh-arculator'
    [void](Wait-OrdinaryLog $run 'Firmware: ArcDroid test ROM')
    $fresh = Capture-StableRawScreenshot $run 'rpc-ordinary-fresh-arculator'
    if ($fresh.VisualSignature -ne $ordinaryArcSignature) { throw 'Arculator pixels differ after RPC switch' }
} finally { if ($run) { Stop-MediaRun $run } }
function Invoke-OrdinaryRefusal([string]$Library, [string]$Name, [string]$Pattern) {
    $refusalLog = Join-Path $evidenceDirectory "$Name.log"
    $text = ""
    $arguments = @("--config=$config", "--libretro=$Library", '--verbose', "--log-file=$refusalLog", '--max-frames=180', ('"' + $ordinaryContent + '"'))
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
        if ($text -notmatch $Pattern -or $text -match 'summary: phase=pending .*state=10 ') { throw "$Name did not give its class-A refusal" }
    } finally {
        if (-not $refusal.HasExited) {
            $udp = [System.Net.Sockets.UdpClient]::new();$udp.Connect('127.0.0.1',55355)
            Send-QuitPair $udp;$udp.Dispose()
            if (-not $refusal.WaitForExit(5000)) { Stop-Process -Id $refusal.Id -Force;$refusal.WaitForExit() }
        }
    }
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
$config = '"' + (Resolve-Path $archiveConfig).Path + '"'
Invoke-OrdinaryRefusal $ordinaryCore 'rpc-ordinary-test-j4' 'N-6: First use.*HardDisc4.zip and PlingSystem.zip'
if (Test-Path (Join-Path $archiveSave 'arcdroid/hd-riscpc')) { throw 'Missing archives created a store' }
# J3 uses the unmodified gate-open SHIPPING DLL, with an empty firmware folder.
Get-ChildItem (Join-Path $ordinarySystem 'arcdroid/firmware') | Remove-Item
Invoke-OrdinaryRefusal (Join-Path $ordinaryBuild 'arcdroid_libretro.dll') 'rpc-ordinary-shipping-j3' 'N-1: Needs RiscPC'
'R3d-4 test-core: first use, archive-free reopen, primary run-ahead/rewind refusal, same-process Arculator switch and raw-pixel baseline; shipping J3. J4 is test-core evidence.' |
    Set-Content -Encoding ASCII (Join-Path $evidenceDirectory 'rpc-ordinary-result.txt')
