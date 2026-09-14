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
audio_driver = "null"
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
"@ | Set-Content -Encoding ASCII $config

$log = Join-Path $evidenceDirectory "retroarch.log"
$screenshot = Join-Path $evidenceDirectory "arcdroid-pattern.png"
function Invoke-ArcDroid([int]$Frames, [bool]$TakeScreenshot) {
    $arguments = @(
        "--config=$config", "--libretro=$core", "--verbose",
        "--max-frames=$Frames"
    )
    if ($TakeScreenshot) {
        $arguments += "--max-frames-ss"
        $arguments += "--max-frames-ss-path=$screenshot"
    }
    $arguments += $content
    $output = & $retroArch @arguments 2>&1
    $exitCode = $LASTEXITCODE
    $output | Tee-Object -FilePath $log -Append | Write-Host
    if ($exitCode -ne 0) { throw "RetroArch exited with code $exitCode" }
}

Invoke-ArcDroid 600 $true
for ($reload = 1; $reload -le 10; $reload++) {
    Write-Host "RetroArch content reload $reload/10"
    Invoke-ArcDroid 100 $false
}

if (-not (Test-Path $screenshot) -or (Get-Item $screenshot).Length -lt 1024) {
    throw "RetroArch did not produce the required pattern screenshot"
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
