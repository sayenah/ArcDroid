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
New-Item -ItemType Directory -Force $coreDirectory, $infoDirectory, $systemDirectory, $saveDirectory | Out-Null

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
        $quitBytes = [Text.Encoding]::ASCII.GetBytes("QUIT")
        [void]$udp.Send($quitBytes, $quitBytes.Length)
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
}

# Slice 2a exercises disk control and initial-image replay with the generated
# media ROM. Each network command is followed by an observed screenshot before
# the next command is sent. Separate launches cover valid/stale .ldci records
# and the JFD reader.
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-media-test.rom") $firmware
$playlist = (Resolve-Path (Join-Path $InputDirectory "arcdroid-media.m3u")).Path
$disc1 = (Resolve-Path (Join-Path $InputDirectory "arcdroid-media-disc1.adf")).Path
$disc2 = (Resolve-Path (Join-Path $InputDirectory "arcdroid-media-disc2.adf")).Path
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
            Send-MediaCommand $Run "QUIT"
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

# RetroArch's per-core save sorting redirects this core's save directory to
# <configured savefile_directory>/ArcDroid, and .ldci records live there.
$indexDirectory = Join-Path $saveDirectory "ArcDroid"
New-Item -ItemType Directory -Force $indexDirectory | Out-Null
$indexRecord = Join-Path $indexDirectory "arcdroid-media.ldci"
@{ version = "1.0"; image_index = 1; image_path = $disc2 } |
    ConvertTo-Json | Set-Content -Encoding ASCII $indexRecord
$validRun = Start-MediaRun $playlist "ldci-valid"
try {
    [void](Capture-ExpectedSignature $validRun "ldci-valid-disc2" $disc2Signature $false)
} finally {
    Stop-MediaRun $validRun
}

@{ version = "1.0"; image_index = 1; image_path = "$disc2.stale" } |
    ConvertTo-Json | Set-Content -Encoding ASCII $indexRecord
$staleRun = Start-MediaRun $playlist "ldci-stale"
try {
    [void](Capture-ExpectedSignature $staleRun "ldci-stale-disc1" $disc1Signature $false)
} finally {
    Stop-MediaRun $staleRun
}

$jfdRun = Start-MediaRun $jfd "jfd"
try {
    [void](Capture-ExpectedSignature $jfdRun "jfd-golden" $disc1Signature $false)
} finally {
    Stop-MediaRun $jfdRun
}

Write-Host "Slice 2a Windows RetroArch: disk commands, valid/stale initial image, and JFD golden region passed"
if ($env:GITHUB_STEP_SUMMARY) {
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 2a media: playlist disc 1 observed; eject/next/close observed before successive commands; disc 2 region observed."
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Initial disk replay: valid .ldci started on disc 2; stale .ldci started on disc 1."
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- JFD: generated fixture reached the disc 1 golden result region."
}
