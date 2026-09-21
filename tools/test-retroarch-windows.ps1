# ArcDroid native RetroArch validation. GPL-2.0-only.
param(
    [Parameter(Mandatory = $true)][string]$InputDirectory,
    [Parameter(Mandatory = $true)][string]$WorkDirectory
)

$ErrorActionPreference = "Stop"
$retroArchVersion = "1.22.2"
$retroArchSha256 = "b2139b1d0f9d4526dc6b5ce23cbb3efdc766096fa6f2c3df016818b486ac6372"
$retroArchUrl = "https://buildbot.libretro.com/stable/$retroArchVersion/windows/x86_64/RetroArch.7z"
$archive = Join-Path $WorkDirectory "RetroArch.7z"
$extractDirectory = Join-Path $WorkDirectory "distribution"
$evidenceDirectory = Join-Path $WorkDirectory "evidence"

New-Item -ItemType Directory -Force $WorkDirectory, $extractDirectory, $evidenceDirectory | Out-Null
curl.exe --fail --location --silent --show-error $retroArchUrl --output $archive
if ($LASTEXITCODE -ne 0) { throw "RetroArch download failed with exit code $LASTEXITCODE" }
$actualSha256 = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
Write-Host "RetroArch $retroArchVersion archive SHA-256: $actualSha256"
if ($actualSha256 -ne $retroArchSha256) {
    throw "RetroArch archive digest mismatch: expected $retroArchSha256"
}

& 7z x -y "-o$extractDirectory" $archive | Out-Null
if ($LASTEXITCODE -ne 0) { throw "7-Zip failed with exit code $LASTEXITCODE" }
$retroArchDirectory = Join-Path $extractDirectory "RetroArch-Win64"
$retroArch = Join-Path $retroArchDirectory "retroarch.exe"
$coreDirectory = Join-Path $retroArchDirectory "cores"
$infoDirectory = Join-Path $retroArchDirectory "info"
$systemDirectory = Join-Path $retroArchDirectory "system\arcdroid\firmware"
$saveDirectory = Join-Path $WorkDirectory "save"
$coreOptionsDirectory = Join-Path $retroArchDirectory "config\ArcDroid"
$coreOptions = Join-Path $coreOptionsDirectory "ArcDroid.opt"
New-Item -ItemType Directory -Force $coreDirectory, $infoDirectory, $systemDirectory, `
    $saveDirectory, $coreOptionsDirectory | Out-Null

# GitHub's Windows runners have no physical audio endpoint. The official
# stable build includes SDL2, whose dummy backend still exercises RetroArch's
# audio pipeline without producing frontend initialization errors.
$env:SDL_AUDIODRIVER = "dummy"

$core = Join-Path $coreDirectory "arcdroid_libretro.dll"
$info = Join-Path $infoDirectory "arcdroid_libretro.info"
$firmware = Join-Path $systemDirectory "renamed-test-firmware.data"
$content = Join-Path $InputDirectory "arcdroid-test.adf"
Copy-Item (Join-Path $InputDirectory "arcdroid_libretro.dll") $core
Copy-Item (Join-Path $InputDirectory "arcdroid_libretro.info") $info
Copy-Item (Join-Path $InputDirectory "arcdroid-test.rom") $firmware

function ConfigPath([string]$Path) { return $Path.Replace('\', '/') }
function Send-QuitPair($Udp) {
    $quitBytes = [Text.Encoding]::ASCII.GetBytes("QUIT")
    [void]$Udp.Send($quitBytes, $quitBytes.Length)
    Start-Sleep -Milliseconds 300
    [void]$Udp.Send($quitBytes, $quitBytes.Length)
}
$config = Join-Path $WorkDirectory "retroarch-ci.cfg"
@"
video_driver = "gdi"
audio_driver = "sdl2"
audio_enable = "true"
record_driver = "wav"
midi_driver = "null"
midi_input = "Null"
midi_output = "Null"
input_driver = "dinput"
libretro_directory = "$(ConfigPath $coreDirectory)"
libretro_info_path = "$(ConfigPath $infoDirectory)"
system_directory = "$(ConfigPath (Split-Path (Split-Path $systemDirectory)))"
savefile_directory = "$(ConfigPath $saveDirectory)"
savestate_directory = "$(ConfigPath $saveDirectory)"
screenshot_directory = "$(ConfigPath $evidenceDirectory)"
log_verbosity = "true"
libretro_log_level = "0"
pause_nonactive = "false"
video_vsync = "false"
network_cmd_enable = "true"
network_cmd_port = "55355"
quit_press_twice = "false"
initial_disk_change_enable = "true"
"@ | Set-Content -Encoding ASCII $config

$log = Join-Path $evidenceDirectory "retroarch.log"
$screenshot = Join-Path $evidenceDirectory "arcdroid-pattern.png"
$tone = Join-Path $evidenceDirectory "arcdroid-tone.wav"
function Invoke-ArcDroid([int]$Frames, [bool]$TakeScreenshot) {
    $arguments = @(
        "--config=$config", "--libretro=$core", "--verbose",
        "--max-frames=$Frames"
    )
    if ($TakeScreenshot) {
        $arguments += "--max-frames-ss"
        $arguments += "--max-frames-ss-path=$screenshot"
        $arguments += "--record=$tone"
    }
    $arguments += $content
    # retroarch.exe is a Windows GUI-subsystem executable. PowerShell does not
    # wait for such processes when they are invoked with &, so an immediate
    # return here would race the screenshot and make the reload check vacuous.
    $runId = [Guid]::NewGuid().ToString("N")
    $stdout = Join-Path $WorkDirectory "retroarch-$runId.stdout.log"
    $stderr = Join-Path $WorkDirectory "retroarch-$runId.stderr.log"
    $process = Start-Process -FilePath $retroArch -ArgumentList $arguments `
        -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $output = @()
    if (Test-Path $stdout) { $output += Get-Content $stdout }
    if (Test-Path $stderr) { $output += Get-Content $stderr }
    $output | Tee-Object -FilePath $log -Append | Write-Host
    Remove-Item -Force -ErrorAction SilentlyContinue $stdout, $stderr
    if ($process.ExitCode -ne 0) { throw "RetroArch exited with code $($process.ExitCode)" }
}

Invoke-ArcDroid 600 $true

# RetroArch's network command API defines LOAD_CONTENT as
# "<core path>|<content path>". Try ten reloads inside one process and retain
# the attempt log. If this stable binary does not expose/respond on UDP 55355,
# fall back to ten explicit process relaunches and say so in the job summary.
function Try-InSessionReloads {
    $arguments = @(
        "--config=$config", "--libretro=$core", "--verbose",
        "--max-frames=12000", $content
    )
    $stdout = Join-Path $WorkDirectory "network-reload.stdout.log"
    $stderr = Join-Path $WorkDirectory "network-reload.stderr.log"
    $attemptLog = Join-Path $evidenceDirectory "network-reload-attempt.log"
    $process = Start-Process -FilePath $retroArch -ArgumentList $arguments `
        -PassThru -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    try {
        Start-Sleep -Seconds 2
        if ($process.HasExited) { throw "RetroArch exited before the network probe" }
        $udp = [System.Net.Sockets.UdpClient]::new()
        $udp.Client.ReceiveTimeout = 3000
        $udp.Connect("127.0.0.1", 55355)
        $statusBytes = [Text.Encoding]::ASCII.GetBytes("GET_STATUS")
        [void]$udp.Send($statusBytes, $statusBytes.Length)
        $remote = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
        $statusReply = [Text.Encoding]::ASCII.GetString($udp.Receive([ref]$remote))
        if (-not $statusReply) { throw "empty GET_STATUS reply" }
        $loadCommand = "LOAD_CONTENT $(ConfigPath $core)|$(ConfigPath $content)"
        for ($reload = 1; $reload -le 10; $reload++) {
            Write-Host "RetroArch in-session content reload $reload/10"
            $bytes = [Text.Encoding]::ASCII.GetBytes($loadCommand)
            [void]$udp.Send($bytes, $bytes.Length)
            Start-Sleep -Milliseconds 500
            if ($process.HasExited) { throw "RetroArch exited during in-session reload $reload" }
        }
        Send-QuitPair $udp
        $udp.Dispose()
        if (-not $process.WaitForExit(30000)) { throw "RetroArch did not exit after QUIT" }
        $process.WaitForExit()
        $output = @()
        if (Test-Path $stdout) { $output += Get-Content $stdout }
        if (Test-Path $stderr) { $output += Get-Content $stderr }
        $output | Set-Content -Encoding UTF8 $attemptLog
        $identifications = @($output | Select-String -SimpleMatch "Firmware: ArcDroid test ROM").Count
        if ($process.ExitCode -ne 0 -or $identifications -lt 11) {
            throw "network commands did not produce 10 observable reloads (firmware identifications=$identifications)"
        }
        $output | Add-Content -Encoding UTF8 $log
        return $true
    }
    catch {
        if (Test-Path $stdout) { Get-Content $stdout | Add-Content -Encoding UTF8 $attemptLog }
        if (Test-Path $stderr) { Get-Content $stderr | Add-Content -Encoding UTF8 $attemptLog }
        $_ | Out-String | Add-Content -Encoding UTF8 $attemptLog
        return $false
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
        Remove-Item -Force -ErrorAction SilentlyContinue $stdout, $stderr
    }
}

$inSessionReloads = Try-InSessionReloads
if (-not $inSessionReloads) {
    for ($reload = 1; $reload -le 10; $reload++) {
        Write-Host "RetroArch process-relaunch content load $reload/10"
        Invoke-ArcDroid 100 $false
    }
}

if (-not (Test-Path $screenshot) -or (Get-Item $screenshot).Length -lt 128) {
    throw "RetroArch did not produce the required pattern screenshot"
}

# The generated ADF's first-sector XOR is 0xcb. The authored ROM expands that
# byte and its complement (0x34), and the programmed VIDC palette renders them
# as cyan (#00aaff) and red (#bf1500). Require both payload-derived colours
# and non-uniform output; a generic fixed blank/test image cannot satisfy this.
Add-Type -AssemblyName System.Drawing
$bitmap = [System.Drawing.Bitmap]::new($screenshot)
$cyanPixels = 0
$redPixels = 0
$colours = [System.Collections.Generic.HashSet[int]]::new()
for ($y = 0; $y -lt $bitmap.Height; $y++) {
    for ($x = 0; $x -lt $bitmap.Width; $x++) {
        $pixel = $bitmap.GetPixel($x, $y)
        [void]$colours.Add($pixel.ToArgb())
        if ($pixel.R -le 16 -and $pixel.G -ge 150 -and
            $pixel.G -le 190 -and $pixel.B -ge 235) { $cyanPixels++ }
        if ($pixel.R -ge 175 -and $pixel.R -le 210 -and
            $pixel.G -le 40 -and $pixel.B -le 16) { $redPixels++ }
    }
}
$dimensions = "$($bitmap.Width)x$($bitmap.Height)"
$bitmap.Dispose()
"dimensions=$dimensions unique_colours=$($colours.Count) payload_cyan=$cyanPixels payload_red=$redPixels" |
    Set-Content -Encoding ASCII (Join-Path $evidenceDirectory "screenshot-analysis.txt")
if ($colours.Count -ne 2 -or $cyanPixels -lt 1000 -or $redPixels -lt 1000) {
    throw "Screenshot lacks the expected payload-derived cyan/red signature"
}
if (-not (Select-String -Quiet -SimpleMatch "Firmware: ArcDroid test ROM" $log)) {
    throw "RetroArch log does not contain the test-ROM firmware identification"
}
$errors = Select-String -Pattern '\[ERROR\]' $log
if ($errors) {
    $errors | ForEach-Object { Write-Error $_.Line }
    throw "RetroArch emitted error log lines"
}
$a3000ScreenshotHash = (Get-FileHash -Algorithm SHA256 $screenshot).Hash.ToLowerInvariant()

# Slice 4: select A3010/ARM250 through RetroArch's real core-options path. The
# generated ROM perturbs its payload-derived palette only after the 82C711 DMA
# result phase, so a different screenshot plus the launch log proves that the
# native frontend executed the New-IO branch.
@"
arcdroid_machine = "A3010"
arcdroid_cpu = "ARM250"
arcdroid_joystick_if = "A3010"
"@ | Set-Content -Encoding ASCII $coreOptions
Invoke-ArcDroid 600 $true
$newioScreenshotHash = (Get-FileHash -Algorithm SHA256 $screenshot).Hash.ToLowerInvariant()
if ($newioScreenshotHash -eq $a3000ScreenshotHash) {
    throw "A3010 screenshot matched A3000; generated New-IO branch was not observed"
}
if (-not (Select-String -Quiet -SimpleMatch "Launch options: machine=A3010 cpu=ARM250" $log)) {
    throw "RetroArch log does not identify the A3010/ARM250 launch"
}
Copy-Item -Force $screenshot (Join-Path $evidenceDirectory "arcdroid-newio-pattern.png")
@"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
"@ | Set-Content -Encoding ASCII $coreOptions
Write-Host "RetroArch ${retroArchVersion}: initial 600-frame run and 10 content reloads passed"
Write-Host "Screenshot SHA-256: $((Get-FileHash -Algorithm SHA256 $screenshot).Hash.ToLowerInvariant())"
$reloadEvidence = if ($inSessionReloads) {
    "10 in-session reloads passed through the RetroArch UDP LOAD_CONTENT command"
} else {
    "RetroArch network reload was unavailable; the 10 reload checks are process relaunches"
}
$audioEvidence = "Tone not verifiable in CI: the RetroArch WAV recording driver did not produce a usable file"
if (Test-Path $tone) {
    $waveBytes = [IO.File]::ReadAllBytes($tone)
    $waveHeader = if ($waveBytes.Length -ge 12) { [Text.Encoding]::ASCII.GetString($waveBytes, 0, 12) } else { "" }
    $nonzeroPcmBytes = 0
    for ($index = 44; $index -lt $waveBytes.Length; $index++) {
        if ($waveBytes[$index] -ne 0) { $nonzeroPcmBytes++ }
    }
    if ($waveHeader.StartsWith("RIFF") -and $waveHeader.EndsWith("WAVE") -and $nonzeroPcmBytes -gt 1000) {
        $audioEvidence = "WAV recording captured by RetroArch: $($waveBytes.Length) bytes, $nonzeroPcmBytes non-zero PCM bytes"
    }
}
Write-Host $reloadEvidence
Write-Host $audioEvidence
if ($env:GITHUB_STEP_SUMMARY) {
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "### ArcDroid native Windows RetroArch"
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Screenshot: expected payload-derived cyan/red pattern observed ($dimensions; $cyanPixels/$redPixels pixels)."
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Reloads: $reloadEvidence."
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Audio: $audioEvidence."
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 4 New-IO: A3010/ARM250 launch logged and the executed-branch screenshot differed from A3000."
}

# Slice 2a exercises disk control with the generated media ROM. Each network
# command is followed by an observed screenshot before the next command is
# sent. Separate launches prove that disk-index recording is disabled and
# retain the JFD reader check.
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-media-test.rom") $firmware
$playlist = (Resolve-Path (Join-Path $InputDirectory "arcdroid-media.m3u")).Path
$disc1 = (Resolve-Path (Join-Path $InputDirectory "arcdroid-media-disc1.adf")).Path
$disc2 = (Resolve-Path (Join-Path $InputDirectory "arcdroid-media-disc2.adf")).Path
$folderSet = (Resolve-Path (Join-Path $InputDirectory "Qaventra.adf\Qaventra.adf")).Path
$jfd = (Resolve-Path (Join-Path $InputDirectory "arcdroid-test-two-member.jfd")).Path

function Start-MediaRun([string]$MediaPath, [string]$Name) {
    $stdout = Join-Path $WorkDirectory "$Name.stdout.log"
    $stderr = Join-Path $WorkDirectory "$Name.stderr.log"
    $retroLog = Join-Path $evidenceDirectory "$Name.log"
    Remove-Item -Force -ErrorAction SilentlyContinue $retroLog
    $arguments = @("--config=$config", "--libretro=$core", "--verbose",
        "--log-file=$retroLog", $MediaPath)
    $process = Start-Process -FilePath $retroArch -ArgumentList $arguments `
        -PassThru -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $udp = [System.Net.Sockets.UdpClient]::new()
    $udp.Client.ReceiveTimeout = 500
    $udp.Connect("127.0.0.1", 55355)
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $ready = $false
    while ([DateTime]::UtcNow -lt $deadline -and -not $ready) {
        if ($process.HasExited) { throw "$Name exited before its command port became ready" }
        try {
            $bytes = [Text.Encoding]::ASCII.GetBytes("GET_STATUS")
            [void]$udp.Send($bytes, $bytes.Length)
            $remote = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
            $reply = [Text.Encoding]::ASCII.GetString($udp.Receive([ref]$remote))
            $ready = -not [string]::IsNullOrWhiteSpace($reply)
        } catch [System.Net.Sockets.SocketException] {
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $ready) { throw "$Name did not answer GET_STATUS within 10 seconds" }
    Start-Sleep -Seconds 2
    return @{ Process = $process; Udp = $udp; Stdout = $stdout; Stderr = $stderr;
        RetroLog = $retroLog; Name = $Name }
}

function Send-MediaCommand($Run, [string]$Command) {
    if ($Run.Process.HasExited) { throw "$($Run.Name) exited before $Command" }
    $bytes = [Text.Encoding]::ASCII.GetBytes($Command)
    [void]$Run.Udp.Send($bytes, $bytes.Length)
}

function Stop-MediaRun($Run) {
    try {
        if (-not $Run.Process.HasExited) {
            Send-QuitPair $Run.Udp
            if (-not $Run.Process.WaitForExit(30000)) {
                throw "$($Run.Name) did not exit within 30 seconds after QUIT"
            }
            $Run.Process.WaitForExit()
        }
        if ($Run.Process.ExitCode -ne 0) {
            throw "$($Run.Name) exited with code $($Run.Process.ExitCode)"
        }
    } finally {
        $Run.Udp.Dispose()
        if (-not $Run.Process.HasExited) {
            Stop-Process -Id $Run.Process.Id -Force -ErrorAction SilentlyContinue
        }
        $output = @()
        if (Test-Path $Run.Stdout) { $output += Get-Content $Run.Stdout }
        if (Test-Path $Run.Stderr) { $output += Get-Content $Run.Stderr }
        $output | Set-Content -Encoding UTF8 (Join-Path $evidenceDirectory "$($Run.Name)-process.log")
        Remove-Item -Force -ErrorAction SilentlyContinue $Run.Stdout, $Run.Stderr
        if (-not (Test-Path $Run.RetroLog)) {
            throw "$($Run.Name) did not produce its RetroArch log file"
        }
    }
}

function Get-MediaRowSignature([string]$Path, [string]$Name) {
    $image = [System.Drawing.Bitmap]::new($Path)
    $bestRatio = 0.0
    $bestColour = 0
    $bestY = -1
    try {
        for ($y = 0; $y -lt $image.Height; $y++) {
            $counts = @{}
            $sampleCount = 0
            for ($x = 0; $x -lt $image.Width; $x += 4) {
                $argb = $image.GetPixel($x, $y).ToArgb()
                $rgb = $argb -band 0x00ffffff
                if ($rgb -eq 0) { continue }
                if ($counts.ContainsKey($argb)) { $counts[$argb]++ } else { $counts[$argb] = 1 }
                $sampleCount++
            }
            if ($sampleCount -eq 0) { continue }
            $dominant = $counts.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1
            $ratio = [double]$dominant.Value / $sampleCount
            if ($ratio -gt $bestRatio) {
                $bestRatio = $ratio
                $bestColour = [int]$dominant.Key
                $bestY = $y
            }
        }
    } finally {
        $dimensions = "$($image.Width)x$($image.Height)"
        $image.Dispose()
    }
    if ($bestY -lt 0 -or $bestRatio -lt 0.85) {
        throw "$Name did not contain a machine-decodable uniform media result row"
    }
    $signature = "{0:x8}" -f $bestColour
    "$Name path=$Path dimensions=$dimensions row=$bestY ratio=$bestRatio argb=$signature" |
        Add-Content -Encoding ASCII (Join-Path $evidenceDirectory "media-signatures.txt")
    return $signature
}

function Capture-MediaSignature($Run, [string]$Name) {
    $before = @((Get-ChildItem -Path $evidenceDirectory -Filter "*.png" -ErrorAction SilentlyContinue).FullName)
    Send-MediaCommand $Run "SCREENSHOT"
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $shot = $null
    while ([DateTime]::UtcNow -lt $deadline -and -not $shot) {
        Start-Sleep -Milliseconds 100
        $shot = Get-ChildItem -Path $evidenceDirectory -Filter "*.png" -ErrorAction SilentlyContinue |
            Where-Object { $before -notcontains $_.FullName } |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    }
    if (-not $shot) { throw "$Name screenshot was not observed within 10 seconds" }
    $renamed = Join-Path $evidenceDirectory "$Name.png"
    Move-Item -Force $shot.FullName $renamed
    return Get-MediaRowSignature $renamed $Name
}

function Capture-ExpectedSignature($Run, [string]$Name, [string]$Expected, [bool]$Different) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $attempt = 0
    do {
        $attempt++
        $actual = Capture-MediaSignature $Run "$Name-$attempt"
        $matched = $actual -eq $Expected
        if ($matched -ne $Different) { return $actual }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    $relation = if ($Different) { "different from" } else { "equal to" }
    throw "$Name did not become $relation $Expected within 10 seconds (last $actual)"
}

# RetroArch's per-core save sorting redirects this core's save directory to
# <configured savefile_directory>/ArcDroid, and .ldci records live there.
$indexDirectory = Join-Path $saveDirectory "ArcDroid"
New-Item -ItemType Directory -Force $indexDirectory | Out-Null
$indexRecord = Join-Path $indexDirectory "arcdroid-media.ldci"
Remove-Item -Force -ErrorAction SilentlyContinue $indexRecord
function Assert-No-LdciRecords([string]$Context) {
    $records = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "*.ldci" -File -Recurse)
    if ($records.Count -ne 0) {
        throw "$Context unexpectedly created .ldci records: $($records.FullName -join ', ')"
    }
}

$mediaRun = Start-MediaRun $playlist "playlist-disk-control"
try {
    $disc1Signature = Capture-MediaSignature $mediaRun "playlist-disc1"
    Send-MediaCommand $mediaRun "DISK_EJECT_TOGGLE"
    [void](Capture-ExpectedSignature $mediaRun "playlist-ejected" $disc1Signature $false)
    Send-MediaCommand $mediaRun "DISK_NEXT"
    [void](Capture-ExpectedSignature $mediaRun "playlist-selected-disc2" $disc1Signature $false)
    Send-MediaCommand $mediaRun "DISK_EJECT_TOGGLE"
    $disc2Signature = Capture-ExpectedSignature $mediaRun "playlist-disc2" $disc1Signature $true
} finally {
    Stop-MediaRun $mediaRun
}
$coreSaveDirectory = Join-Path $indexDirectory "arcdroid"
if (-not (Test-Path -LiteralPath $coreSaveDirectory -PathType Container)) {
    throw "ArcDroid save directory was not created under RetroArch's per-core save directory"
}
Assert-No-LdciRecords "Swap and clean quit"

$relaunchRun = Start-MediaRun $playlist "playlist-swap-relaunch"
try {
    [void](Capture-ExpectedSignature $relaunchRun "playlist-relaunch-disc1" $disc1Signature $false)
} finally {
    Stop-MediaRun $relaunchRun
}
Assert-No-LdciRecords "Relaunch"

$folderRun = Start-MediaRun $folderSet "folder-set-disk-control"
try {
    [void](Capture-ExpectedSignature $folderRun "folder-set-disc1" $disc1Signature $false)
    Send-MediaCommand $folderRun "DISK_EJECT_TOGGLE"
    [void](Capture-ExpectedSignature $folderRun "folder-set-ejected" $disc1Signature $false)
    Send-MediaCommand $folderRun "DISK_NEXT"
    [void](Capture-ExpectedSignature $folderRun "folder-set-selected-disc2" $disc1Signature $false)
    Send-MediaCommand $folderRun "DISK_EJECT_TOGGLE"
    [void](Capture-ExpectedSignature $folderRun "folder-set-disc2" $disc2Signature $false)
} finally {
    Stop-MediaRun $folderRun
}
Assert-No-LdciRecords "Folder-set run"

@{ version = "1.0"; image_index = 1; image_path = $disc2 } |
    ConvertTo-Json | Set-Content -Encoding ASCII $indexRecord
$seededRecord = [Convert]::ToBase64String([IO.File]::ReadAllBytes($indexRecord))
$seededRun = Start-MediaRun $playlist "ldci-seeded"
try {
    [void](Capture-ExpectedSignature $seededRun "ldci-seeded-disc1" $disc1Signature $false)
} finally {
    Stop-MediaRun $seededRun
}
if (Select-String -Path $seededRun.RetroLog -SimpleMatch "Failed to set initial" -Quiet) {
    throw "Seeded .ldci run logged an initial-disk failure"
}
$seededRecordAfter = [Convert]::ToBase64String([IO.File]::ReadAllBytes($indexRecord))
if ($seededRecordAfter -ne $seededRecord) {
    throw "Seeded .ldci record changed during the clean launch and quit"
}
$ldciRecords = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "*.ldci" -File -Recurse)
if ($ldciRecords.Count -ne 1 -or
    $ldciRecords[0].FullName -ne [IO.Path]::GetFullPath($indexRecord)) {
    throw "Seeded run did not leave exactly the seeded .ldci record"
}

$jfdRun = Start-MediaRun $jfd "jfd"
try {
    [void](Capture-ExpectedSignature $jfdRun "jfd-golden" $disc1Signature $false)
} finally {
    Stop-MediaRun $jfdRun
}

Write-Host "Slice 2a Windows RetroArch: disk commands, image-1 relaunch, inert seeded .ldci, and JFD golden region passed"
Write-Host "Slice 5b Windows RetroArch: folder disc 1 and eject/next/insert matched the playlist disc-2 signature"
if ($env:GITHUB_STEP_SUMMARY) {
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 2a media: playlist disc 1 observed; eject/next/close observed before successive commands; disc 2 region observed."
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 5b-2 boot image: swap, clean quit and relaunch started on disc 1 without creating an .ldci; a seeded valid disc-2 record was ignored without an initial-disc error and remained byte-identical."
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- JFD: generated fixture reached the disc 1 golden result region."
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 5b folder set: selected disc observed; eject/next/insert matched the playlist disc-2 signature."
}

# Slice 2b uses the host-test core because the generated firmware is the
# project-authored test ROM, not one of the four shipping RISC OS 3 classes.
# The shipping DLL above has already completed all native runs and guards.
Copy-Item -Force (Join-Path $InputDirectory "arcdroid_libretro_test.dll") $core
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-test.rom") $firmware
$content = (Resolve-Path (Join-Path $InputDirectory "arcdroid-application-l.adl")).Path
Invoke-ArcDroid 100 $false
$launchLines = Get-Content $log | Select-String -SimpleMatch "Launch:"
if (-not ($launchLines.Line -match 'decision=!Demo')) {
    throw "Slice 2b application run did not log the expected Launch: decision"
}
$contentSha1 = (Get-FileHash -Algorithm SHA1 $content).Hash.ToLowerInvariant()
$helpers = @(Get-ChildItem -Path $saveDirectory -Filter "$contentSha1.adl" -File -Recurse |
    Where-Object { $_.DirectoryName -match '[\\/]arcdroid[\\/]launch$' })
if ($helpers.Count -ne 1 -or $helpers[0].Length -ne 655360) {
    throw "Slice 2b application run did not publish exactly one 640 KiB helper under the saves directory"
}
Write-Host "Slice 2b Windows RetroArch: Launch: decision and helper publication passed"
if ($env:GITHUB_STEP_SUMMARY) {
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 2b launch: generated option-0 application produced the Launch: decision and a 640 KiB save helper."
}

# Slice 5a proves that a top-bit guest leaf survives native Windows frontend
# loading, discovery, escaped diagnostics, and helper publication.
$content = (Resolve-Path (Join-Path $InputDirectory "checkpoint-a-top-bit.adf")).Path
Invoke-ArcDroid 100 $false
$topBitLaunchLines = Get-Content $log | Select-String -SimpleMatch "Launch:"
$topBitDecision = $topBitLaunchLines | Where-Object {
    $_.Line.Contains('decision=!Z\xF6rvex') -and $_.Line.Contains('reason=single application')
}
if (-not $topBitDecision) {
    throw "Slice 5a top-bit application run did not log its escaped Launch: decision"
}
$contentSha1 = (Get-FileHash -Algorithm SHA1 $content).Hash.ToLowerInvariant()
$helpers = @(Get-ChildItem -Path $saveDirectory -Filter "$contentSha1.adl" -File -Recurse |
    Where-Object { $_.DirectoryName -match '[\\/]arcdroid[\\/]launch$' })
if ($helpers.Count -ne 1 -or $helpers[0].Length -ne 655360) {
    throw "Slice 5a top-bit application run did not publish exactly one 640 KiB helper"
}
Write-Host "Slice 5a Windows RetroArch: top-bit Launch: decision and helper publication passed"
if ($env:GITHUB_STEP_SUMMARY) {
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 5a launch: top-bit application produced the escaped Launch: decision and a 640 KiB save helper."
}

# Slice 5c is intentionally interactive: no choice record is seeded. Capture
# explicit Application 1 and Application 2 guest signatures, show the chooser
# under Auto,
# inject Down/Return into RetroArch's actual window, and prove both the pick
# and its subsequent remembered replay from frontend screenshots and logs.
function Get-RunLogTail($Run) {
    if (Test-Path -LiteralPath $Run.RetroLog) {
        return (@((Get-Content $Run.RetroLog -Tail 20) -replace '[\r\n]+', ' ') -join ' | ')
    }
    return "<missing $($Run.RetroLog)>"
}

function Capture-RawScreenshot($Run, [string]$Name) {
    $before = @{}
    Get-ChildItem -Path $evidenceDirectory -Filter "*.png" -ErrorAction SilentlyContinue |
        ForEach-Object { $before[$_.FullName] = "$($_.LastWriteTimeUtc.Ticks):$($_.Length)" }
    Send-MediaCommand $Run "SCREENSHOT"
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $shot = $null
    while ([DateTime]::UtcNow -lt $deadline -and -not $shot) {
        Start-Sleep -Milliseconds 100
        $shot = Get-ChildItem -Path $evidenceDirectory -Filter "*.png" -ErrorAction SilentlyContinue |
            Where-Object {
                $stamp = "$($_.LastWriteTimeUtc.Ticks):$($_.Length)"
                (-not $before.ContainsKey($_.FullName)) -or
                    $before[$_.FullName] -ne $stamp
            } |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    }
    if (-not $shot) {
        $processState = if ($Run.Process.HasExited) {
            "exited code=$($Run.Process.ExitCode)"
        } else { "running pid=$($Run.Process.Id)" }
        $files = @(Get-ChildItem -Path $evidenceDirectory -ErrorAction SilentlyContinue |
            Sort-Object Name | ForEach-Object {
                "$($_.Name):$($_.Length)b:$($_.LastWriteTimeUtc.ToString('o'))"
            }) -join ", "
        $logTail = Get-RunLogTail $Run
        throw "$Name screenshot step timed out after 10 seconds; process=$processState; evidence_files=[$files]; retroarch_log_tail=[$logTail]"
    }
    $renamed = Join-Path $evidenceDirectory "$Name.png"
    Move-Item -Force $shot.FullName $renamed
    $image = [System.Drawing.Bitmap]::new($renamed)
    $sampledColours = [System.Collections.Generic.HashSet[int]]::new()
    $canonical = $null
    $graphics = $null
    $bitmapData = $null
    $pixelHasher = $null
    try {
        # RetroArch screenshots are frontend-sized representations of the
        # current core frame. Hash every decoded pixel: the chooser fixture's
        # guest applications can differ in a narrow row that a grid sampler
        # misses when the woven renderer exposes the mode as 640x512.
        $canonical = [System.Drawing.Bitmap]::new(
            $image.Width, $image.Height,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($canonical)
        $graphics.DrawImageUnscaled($image, 0, 0)
        $graphics.Dispose()
        $graphics = $null
        $rectangle = [System.Drawing.Rectangle]::new(
            0, 0, $canonical.Width, $canonical.Height)
        $bitmapData = $canonical.LockBits(
            $rectangle,
            [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $rowBytes = $canonical.Width * 4
        $pixels = [byte[]]::new($rowBytes * $canonical.Height)
        for ($y = 0; $y -lt $canonical.Height; $y++) {
            $source = [IntPtr]::Add(
                $bitmapData.Scan0, $y * $bitmapData.Stride)
            [Runtime.InteropServices.Marshal]::Copy(
                $source, $pixels, $y * $rowBytes, $rowBytes)
            for ($x = 0; $x -lt $canonical.Width; $x++) {
                $offset = $y * $rowBytes + $x * 4
                $rgb = (([int]$pixels[$offset + 2] -shl 16) -bor ([int]$pixels[$offset + 1] -shl 8) -bor [int]$pixels[$offset])
                [void]$sampledColours.Add($rgb)
            }
        }
        $canonical.UnlockBits($bitmapData)
        $bitmapData = $null
        $pixelHasher = [Security.Cryptography.SHA256]::Create()
        $visualSignature = ([BitConverter]::ToString(
            $pixelHasher.ComputeHash($pixels))).Replace("-", "").ToLowerInvariant()
        $dimensions = "$($image.Width)x$($image.Height)"
        $backgroundPixel = "{0:x6}" -f
            ($image.GetPixel(0, 0).ToArgb() -band 0x00ffffff)
        $chooserFocusPixel = "{0:x6}" -f
            ($image.GetPixel(60, 100).ToArgb() -band 0x00ffffff)
    } finally {
        if ($pixelHasher) { $pixelHasher.Dispose() }
        if ($bitmapData) { $canonical.UnlockBits($bitmapData) }
        if ($graphics) { $graphics.Dispose() }
        if ($canonical) { $canonical.Dispose() }
        $image.Dispose()
    }
    $hash = (Get-FileHash -Algorithm SHA256 $renamed).Hash.ToLowerInvariant()
    "$Name path=$renamed dimensions=$dimensions sampled_colours=$($sampledColours.Count) background_rgb=$backgroundPixel chooser_focus_rgb=$chooserFocusPixel visual_signature=$visualSignature png_sha256=$hash" |
        Add-Content -Encoding ASCII (Join-Path $evidenceDirectory "launch-chooser-signatures.txt")
    return @{ Path = $renamed; Hash = $hash; Dimensions = $dimensions;
        Colours = $sampledColours.Count; BackgroundPixel = $backgroundPixel;
        ChooserFocusPixel = $chooserFocusPixel; VisualSignature = $visualSignature }
}

function Capture-ExpectedVisualSignature($Run, [string]$Name, $Expected) {
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    $attempt = 0
    do {
        $attempt++
        $capture = Capture-RawScreenshot $Run "$Name-$attempt"
        if ($capture.VisualSignature -eq $Expected.VisualSignature -and
            $capture.Dimensions -eq $Expected.Dimensions) { return $capture }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    $logTail = Get-RunLogTail $Run
    throw "$Name guest-signature step did not reach expected visual_signature=$($Expected.VisualSignature) dimensions=$($Expected.Dimensions) within 12 seconds; observed visual_signature=$($capture.VisualSignature) png_sha256=$($capture.Hash) dimensions=$($capture.Dimensions) sampled_colours=$($capture.Colours); retroarch_log_tail=[$logTail]"
}

function Capture-StableRawScreenshot($Run, [string]$Name) {
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    $attempt = 0
    $previous = $null
    do {
        $attempt++
        $capture = Capture-RawScreenshot $Run "$Name-$attempt"
        if ($previous -and
            $capture.VisualSignature -eq $previous.VisualSignature -and
            $capture.Dimensions -eq $previous.Dimensions) { return $capture }
        $previous = $capture
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    $observed = if ($capture) {
        "visual_signature=$($capture.VisualSignature) png_sha256=$($capture.Hash) dimensions=$($capture.Dimensions) sampled_colours=$($capture.Colours)"
    } else { "no capture" }
    $logTail = Get-RunLogTail $Run
    throw "$Name guest-signature step did not stabilize within 12 seconds; observed $observed; retroarch_log_tail=[$logTail]"
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ArcDroidWindowInput {
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
}
"@
function Send-ChooserKeyboardPick($Run) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $handle = [IntPtr]::Zero
    while ([DateTime]::UtcNow -lt $deadline -and $handle -eq [IntPtr]::Zero) {
        if ($Run.Process.HasExited) {
            throw "$($Run.Name) keyboard-injection step found an exited process code=$($Run.Process.ExitCode); retroarch_log_tail=[$(Get-RunLogTail $Run)]"
        }
        $Run.Process.Refresh()
        $handle = $Run.Process.MainWindowHandle
        if ($handle -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 100 }
    }
    if ($handle -eq [IntPtr]::Zero) {
        throw "$($Run.Name) keyboard-injection step observed no main window within 10 seconds; process_pid=$($Run.Process.Id); retroarch_log_tail=[$(Get-RunLogTail $Run)]"
    }
    [void][ArcDroidWindowInput]::ShowWindow($handle, 9)
    if (-not [ArcDroidWindowInput]::SetForegroundWindow($handle)) {
        throw "$($Run.Name) keyboard-injection step could not foreground window_handle=$handle; process_pid=$($Run.Process.Id); retroarch_log_tail=[$(Get-RunLogTail $Run)]"
    }
    Start-Sleep -Milliseconds 250
    [System.Windows.Forms.SendKeys]::SendWait("{DOWN}")
    Start-Sleep -Milliseconds 150
    [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
}

Copy-Item -Force (Join-Path $InputDirectory "arcdroid-chooser-test.rom") $firmware
$chooserContent = (Resolve-Path (Join-Path $InputDirectory "arcdroid-chooser-l.adl")).Path
$chooserContentSha1 = (Get-FileHash -Algorithm SHA1 $chooserContent).Hash.ToLowerInvariant()
$chooserRecords = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "$chooserContentSha1.choice" -File -Recurse -ErrorAction SilentlyContinue)
if ($chooserRecords.Count) { Remove-Item -Force $chooserRecords.FullName }

@"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_disk_writes = "off"
arcdroid_launch = "app1"
arcdroid_launch_chooser = "remember"
"@ | Set-Content -Encoding ASCII $coreOptions
$explicitApp1Run = Start-MediaRun $chooserContent "launch-chooser-explicit-app1"
try {
    $app1Capture = Capture-StableRawScreenshot $explicitApp1Run "launch-chooser-app1-reference"
} finally {
    Stop-MediaRun $explicitApp1Run
}
if (-not (Select-String -Path $explicitApp1Run.RetroLog -SimpleMatch "decision=!Nulvane" -Quiet)) {
    $observed = @(Get-Content $explicitApp1Run.RetroLog | Select-String -SimpleMatch "Launch:") -join " | "
    throw "Explicit Application 1 oracle did not launch !Nulvane; observed Launch lines=[$observed]"
}
$helpers = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "$chooserContentSha1.adl" -File -Recurse |
    Where-Object { $_.DirectoryName -match '[\\/]arcdroid[\\/]launch$' })
if ($helpers.Count -ne 1) {
    throw "Explicit Application 1 oracle expected one helper, observed $($helpers.Count): $($helpers.FullName -join ', ')"
}
$app1HelperHash = (Get-FileHash -Algorithm SHA256 $helpers[0].FullName).Hash.ToLowerInvariant()
$bindings = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "$chooserContentSha1.bind" -File -Recurse -ErrorAction SilentlyContinue)
if ($bindings.Count) { Remove-Item -Force $bindings.FullName }

@"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_disk_writes = "off"
arcdroid_launch = "app2"
arcdroid_launch_chooser = "remember"
"@ | Set-Content -Encoding ASCII $coreOptions
$explicitApp2Run = Start-MediaRun $chooserContent "launch-chooser-explicit-app2"
try {
    $app2Capture = Capture-StableRawScreenshot $explicitApp2Run "launch-chooser-app2-reference"
} finally {
    Stop-MediaRun $explicitApp2Run
}
if (-not (Select-String -Path $explicitApp2Run.RetroLog -SimpleMatch "decision=!Qaventra" -Quiet)) {
    $observed = @(Get-Content $explicitApp2Run.RetroLog | Select-String -SimpleMatch "Launch:") -join " | "
    throw "Explicit Application 2 oracle did not launch !Qaventra; observed Launch lines=[$observed]"
}
$helpers = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "$chooserContentSha1.adl" -File -Recurse |
    Where-Object { $_.DirectoryName -match '[\\/]arcdroid[\\/]launch$' })
if ($helpers.Count -ne 1) {
    throw "Explicit Application 2 oracle expected one helper, observed $($helpers.Count): $($helpers.FullName -join ', ')"
}
$app2HelperHash = (Get-FileHash -Algorithm SHA256 $helpers[0].FullName).Hash.ToLowerInvariant()
# Guest geometry belongs to the programmed VIDC mode, not the chooser's
# adapter-owned 640x480 surface. Require the two stable reference captures to
# agree with each other instead of assuming how RetroArch exposes that mode.
if ($app1Capture.Dimensions -ne $app2Capture.Dimensions) {
    throw "Application-oracle screenshot step expected matching stable guest dimensions; observed app1=$($app1Capture.Dimensions) app2=$($app2Capture.Dimensions); app1_log_tail=[$(Get-RunLogTail $explicitApp1Run)]; app2_log_tail=[$(Get-RunLogTail $explicitApp2Run)]"
}
if ($app1Capture.VisualSignature -eq $app2Capture.VisualSignature -or
    $app1HelperHash -eq $app2HelperHash) {
    throw "Application-oracle setup was not discriminating: app1 visual_signature=$($app1Capture.VisualSignature) helper=$app1HelperHash; app2 visual_signature=$($app2Capture.VisualSignature) helper=$app2HelperHash; app1_log_tail=[$(Get-RunLogTail $explicitApp1Run)]; app2_log_tail=[$(Get-RunLogTail $explicitApp2Run)]"
}

@"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_disk_writes = "off"
arcdroid_launch = "auto"
arcdroid_launch_chooser = "remember"
"@ | Set-Content -Encoding ASCII $coreOptions
$chooserRun = Start-MediaRun $chooserContent "launch-chooser-interactive"
try {
    $chooserCapture = Capture-RawScreenshot $chooserRun "launch-chooser-visible"
    if ($chooserCapture.Dimensions -ne "640x480" -or $chooserCapture.Colours -lt 4 -or
        $chooserCapture.BackgroundPixel -ne "101a2a" -or
        $chooserCapture.ChooserFocusPixel -ne "a8d8ff" -or
        $chooserCapture.VisualSignature -eq $app1Capture.VisualSignature -or
        $chooserCapture.VisualSignature -eq $app2Capture.VisualSignature) {
        throw "Chooser-recognition step expected the 640x480 ArcDroid renderer with >=4 sampled colours, background_rgb=101a2a and focused-row_rgb=a8d8ff; observed dimensions=$($chooserCapture.Dimensions) sampled_colours=$($chooserCapture.Colours) background_rgb=$($chooserCapture.BackgroundPixel) focused-row_rgb=$($chooserCapture.ChooserFocusPixel) visual_signature=$($chooserCapture.VisualSignature) png_sha256=$($chooserCapture.Hash); app1=$($app1Capture.VisualSignature) app2=$($app2Capture.VisualSignature); retroarch_log_tail=[$(Get-RunLogTail $chooserRun)]"
    }
    Send-ChooserKeyboardPick $chooserRun
    [void](Capture-ExpectedVisualSignature $chooserRun "launch-chooser-picked-app2" $app2Capture)
} finally {
    Stop-MediaRun $chooserRun
}
if (-not (Select-String -Path $chooserRun.RetroLog -SimpleMatch "chooser shown" -Quiet) -or
    -not (Select-String -Path $chooserRun.RetroLog -SimpleMatch "chooser chose target" -Quiet) -or
    -not (Select-String -Path $chooserRun.RetroLog -SimpleMatch "decision=!Qaventra" -Quiet)) {
    $observed = @(Get-Content $chooserRun.RetroLog | Select-String -Pattern 'Launch:|chooser') -join " | "
    throw "Interactive chooser log-verification step expected chooser shown, chooser chose target, and decision=!Qaventra; observed=[$observed]"
}
$helpers = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "$chooserContentSha1.adl" -File -Recurse |
    Where-Object { $_.DirectoryName -match '[\\/]arcdroid[\\/]launch$' })
if ($helpers.Count -ne 1) {
    throw "Interactive chooser helper-verification step expected one helper, observed $($helpers.Count): $($helpers.FullName -join ', ')"
}
$interactiveHelperHash = (Get-FileHash -Algorithm SHA256 $helpers[0].FullName).Hash.ToLowerInvariant()
if ($interactiveHelperHash -ne $app2HelperHash -or $interactiveHelperHash -eq $app1HelperHash) {
    throw "Interactive chooser selected the wrong helper: observed=$interactiveHelperHash expected_app2=$app2HelperHash app1=$app1HelperHash"
}
$chooserRecords = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "$chooserContentSha1.choice" -File -Recurse)
if ($chooserRecords.Count -ne 1) {
    throw "Interactive chooser record-publication step expected exactly one $chooserContentSha1.choice, observed $($chooserRecords.Count): $($chooserRecords.FullName -join ', ')"
}
$rememberedChoice = [Convert]::ToBase64String([IO.File]::ReadAllBytes($chooserRecords[0].FullName))

$rememberedRun = Start-MediaRun $chooserContent "launch-chooser-remembered"
try {
    [void](Capture-ExpectedVisualSignature $rememberedRun "launch-chooser-replayed-app2" $app2Capture)
} finally {
    Stop-MediaRun $rememberedRun
}
if (-not (Select-String -Path $rememberedRun.RetroLog -SimpleMatch "chooser remembered target" -Quiet) -or
    (Select-String -Path $rememberedRun.RetroLog -SimpleMatch "chooser shown" -Quiet)) {
    $observed = @(Get-Content $rememberedRun.RetroLog | Select-String -Pattern 'Launch:|chooser') -join " | "
    throw "Remembered-replay log step expected chooser remembered target and no chooser shown; observed=[$observed]"
}
$helpers = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "$chooserContentSha1.adl" -File -Recurse |
    Where-Object { $_.DirectoryName -match '[\\/]arcdroid[\\/]launch$' })
if ($helpers.Count -ne 1) {
    throw "Remembered-replay helper-verification step expected one helper, observed $($helpers.Count): $($helpers.FullName -join ', ')"
}
$rememberedHelperHash = (Get-FileHash -Algorithm SHA256 $helpers[0].FullName).Hash.ToLowerInvariant()
if ($rememberedHelperHash -ne $app2HelperHash -or $rememberedHelperHash -eq $app1HelperHash) {
    throw "Remembered replay used the wrong helper: observed=$rememberedHelperHash expected_app2=$app2HelperHash app1=$app1HelperHash"
}
$rememberedChoiceAfter = [Convert]::ToBase64String([IO.File]::ReadAllBytes($chooserRecords[0].FullName))
if ($rememberedChoiceAfter -ne $rememberedChoice) {
    throw "Remembered-replay persistence step changed the choice record: before_base64=$rememberedChoice after_base64=$rememberedChoiceAfter"
}
Write-Host "Slice 5c Windows RetroArch: chooser screenshot, injected keyboard pick, guest signature and remembered replay passed"
if ($env:GITHUB_STEP_SUMMARY) {
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 5c chooser: distinct 640x480 chooser observed; Down/Return injected into the RetroArch window selected !Qaventra; relaunch replayed the same guest screenshot without the chooser."
}
