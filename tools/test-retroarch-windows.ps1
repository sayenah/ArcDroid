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
