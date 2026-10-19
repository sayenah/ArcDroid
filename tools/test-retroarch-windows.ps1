# ArcDroid native RetroArch validation. GPL-2.0-only.
param(
    [Parameter(Mandatory = $true)][string]$InputDirectory,
    [Parameter(Mandatory = $true)][string]$WorkDirectory,
    [switch]$GuardedJfdOnly,
    [string]$OrdinaryRouteBuild = ""
)

$ErrorActionPreference = "Stop"
$retroArchVersion = "1.22.2"
$retroArchSha256 = "b2139b1d0f9d4526dc6b5ce23cbb3efdc766096fa6f2c3df016818b486ac6372"
$retroArchUrl = "https://buildbot.libretro.com/stable/$retroArchVersion/windows/x86_64/RetroArch.7z"
$archive = Join-Path $WorkDirectory "RetroArch.7z"
$extractDirectory = Join-Path $WorkDirectory "distribution"
$evidenceDirectory = Join-Path $WorkDirectory "evidence"
$ciRetryAttempts = 3

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
$script:RetroArchStartedProcesses = [System.Collections.Generic.List[object]]::new()
function Wait-RetroArchStartReady {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $live = @($script:RetroArchStartedProcesses | Where-Object {
            $_.Refresh(); -not $_.HasExited
        } | ForEach-Object { $_.Id })
        $holders = @(Get-NetUDPEndpoint -LocalPort 55355 -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty OwningProcess -Unique)
        $known = @($script:RetroArchStartedProcesses | ForEach-Object { $_.Id })
        $foreign = @($holders | Where-Object { $known -notcontains $_ })
        if ($foreign.Count) {
            $details = @($foreign | ForEach-Object {
                $owner = Get-Process -Id $_ -ErrorAction SilentlyContinue
                "pid=$_ name=$(if ($owner) { $owner.ProcessName } else { 'exited' })"
            }) -join ', '
            throw "UDP 55355 is held by a process this script did not start: $details"
        }
        if (-not $live.Count) {
            $probe = [System.Net.Sockets.Socket]::new(
                [System.Net.Sockets.AddressFamily]::InterNetwork,
                [System.Net.Sockets.SocketType]::Dgram,
                [System.Net.Sockets.ProtocolType]::Udp)
            try {
                $probe.ExclusiveAddressUse = $true
                $probe.Bind([System.Net.IPEndPoint]::new(
                    [System.Net.IPAddress]::Any, 55355))
                return
            } catch [System.Net.Sockets.SocketException] {
                # The owner may be exiting between the endpoint query and bind.
            } finally { $probe.Dispose() }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    $holderDetails = @($holders | ForEach-Object {
        $owner = Get-Process -Id $_ -ErrorAction SilentlyContinue
        "pid=$_ name=$(if ($owner) { $owner.ProcessName } else { 'exited' })"
    }) -join ', '
    throw "RetroArch start timed out after 30 s: live script PIDs=$($live -join ',') UDP 55355 holders=$holderDetails"
}
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

function Move-ItemWithSharingRetry([string]$Source, [string]$Destination,
                                   [string]$Context) {
    for ($attempt = 1; $attempt -le $ciRetryAttempts; $attempt++) {
        Write-Host "$Context move attempt $attempt/$ciRetryAttempts"
        try {
            Move-Item -Force -LiteralPath $Source -Destination $Destination -ErrorAction Stop
            return
        } catch {
            $message = $_.Exception.Message
            if ($message -notmatch '(?i)being used by another process') { throw }
            if ($attempt -eq $ciRetryAttempts) {
                throw "$message (attempt $attempt/$ciRetryAttempts)"
            }
            Start-Sleep -Milliseconds 200
        }
    }
}

function Get-EventDataMap($EventRecord) {
    $map = @{}
    try {
        $xml = [xml]$EventRecord.ToXml()
        foreach ($item in @($xml.Event.EventData.Data)) {
            $name = [string]$item.GetAttribute("Name")
            if (-not [string]::IsNullOrWhiteSpace($name)) {
                $map[$name] = [string]$item.'#text'
            }
        }
    } catch {
        # The formatted event message remains useful if one record has an
        # unexpected XML shape. Crash collection must never replace the run's
        # original failure.
    }
    return $map
}

function Get-EventField($Map, [string[]]$Names, $Values, [int]$Fallback) {
    foreach ($name in $Names) {
        if ($Map.ContainsKey($name) -and
            -not [string]::IsNullOrWhiteSpace([string]$Map[$name])) {
            return [string]$Map[$name]
        }
    }
    if ($Fallback -ge 0 -and $Fallback -lt $Values.Count) {
        return [string]$Values[$Fallback]
    }
    return "<not present>"
}

function Write-RetroArchFailureEvidence([string]$Name, $Process,
                                        [DateTime]$StartUtc,
                                        [string[]]$LogPaths,
                                        [string]$Context) {
    # This function is deliberately failure-contained: inability to read the
    # event log or save the evidence must not mask the RetroArch exit that
    # caused this diagnostic path to run.
    try {
        $safeName = $Name -replace '[^A-Za-z0-9_.-]', '_'
        $path = Join-Path $evidenceDirectory "$safeName-crash-forensics.txt"
        $lines = [System.Collections.Generic.List[string]]::new()
        $endUtc = [DateTime]::UtcNow
        $exitCode = "<still running>"
        $pidValue = "<unknown>"
        if ($Process) {
            $pidValue = [string]$Process.Id
            if ($Process.HasExited) { $exitCode = [string]$Process.ExitCode }
        }
        $lines.Add("run=$Name pid=$pidValue exit_code=$exitCode")
        $lines.Add("context=$Context")
        $lines.Add("window_utc=$($StartUtc.ToString('o'))..$($endUtc.ToString('o'))")
        $lines.Add("Windows Application crash events:")

        $matching = @()
        $queryError = $null
        $eventDeadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            try {
                $events = @(Get-WinEvent -FilterHashtable @{
                    LogName = "Application"
                    ProviderName = @("Application Error", "Windows Error Reporting")
                    Id = @(1000, 1001)
                    StartTime = $StartUtc.AddSeconds(-2).ToLocalTime()
                    EndTime = [DateTime]::Now.AddSeconds(1)
                } -ErrorAction SilentlyContinue)
                $matching = @($events | Where-Object {
                    $eventMap = Get-EventDataMap $_
                    $eventValues = @()
                    try {
                        $eventXml = [xml]$_.ToXml()
                        $eventValues = @($eventXml.Event.EventData.Data | ForEach-Object {
                            [string]$_.'#text'
                        })
                    } catch {}
                    $eventText = (($_.Message, ($eventValues -join " ")) -join " ")
                    $appMatch = $eventText -match '(?i)(^|[^A-Za-z0-9_])retroarch\.exe([^A-Za-z0-9_]|$)'
                    $eventPid = Get-EventField $eventMap @("ProcessId") $eventValues -1
                    $pidMatch = $true
                    if ($eventPid -ne "<not present>" -and $Process) {
                        $parsedPid = 0L
                        if ($eventPid -match '^0x[0-9a-fA-F]+$') {
                            $parsedPid = [Convert]::ToInt64($eventPid.Substring(2), 16)
                        } elseif ($eventPid -match '^\d+$') {
                            $parsedPid = [Convert]::ToInt64($eventPid, 10)
                        }
                        if ($parsedPid -ne 0L) { $pidMatch = $parsedPid -eq $Process.Id }
                    }
                    $appMatch -and $pidMatch
                } | Sort-Object RecordId -Unique)
            } catch {
                $queryError = $_.Exception.Message
            }
            $providers = @($matching | Select-Object -ExpandProperty ProviderName -Unique)
            if ($providers -contains "Application Error" -and
                $providers -contains "Windows Error Reporting") { break }
            if ([DateTime]::UtcNow -lt $eventDeadline) {
                Start-Sleep -Milliseconds 500
            }
        } while ([DateTime]::UtcNow -lt $eventDeadline)

        $endUtc = [DateTime]::UtcNow
        $lines[2] = "window_utc=$($StartUtc.ToString('o'))..$($endUtc.ToString('o'))"
        if ($queryError) { $lines.Add("  query_error=$queryError") }
        if ($matching.Count -eq 0) {
            $lines.Add("  <no matching Application Error 1000 or Windows Error Reporting 1001 records found>")
        }
        foreach ($event in $matching) {
            $eventMap = Get-EventDataMap $event
            $eventXml = [xml]$event.ToXml()
            $values = @($eventXml.Event.EventData.Data | ForEach-Object {
                [string]$_.'#text'
            })
            if ($event.Id -eq 1000) {
                $module = Get-EventField $eventMap @("FaultingModuleName", "ModuleName") $values 3
                $exception = Get-EventField $eventMap @("ExceptionCode") $values 6
                $offset = Get-EventField $eventMap @("FaultingOffset", "FaultOffset") $values 7
            } else {
                $module = Get-EventField $eventMap @("P4", "Param4", "param4") $values -1
                $exception = Get-EventField $eventMap @("P7", "Param7", "param7") $values -1
                $offset = Get-EventField $eventMap @("P8", "Param8", "param8") $values -1
            }
            $message = ($event.Message -replace '[\r\n]+', ' ').Trim()
            if ($module -eq "<not present>" -and
                $message -match '(?i)Faulting module name:\s*([^,]+)') {
                $module = $Matches[1].Trim()
            }
            if ($exception -eq "<not present>" -and
                $message -match '(?i)Exception code:\s*(\S+)') {
                $exception = $Matches[1]
            }
            if ($offset -eq "<not present>" -and
                $message -match '(?i)Fault offset:\s*(\S+)') {
                $offset = $Matches[1]
            }
            $lines.Add("  time=$($event.TimeCreated.ToUniversalTime().ToString('o')) provider=$($event.ProviderName) id=$($event.Id) record_id=$($event.RecordId) faulting_module=$module exception_code=$exception fault_offset=$offset")
            $lines.Add("  message=$message")
        }

        $uniqueLogs = @($LogPaths | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_)
        } | Select-Object -Unique)
        foreach ($logPath in $uniqueLogs) {
            $lines.Add("Last 40 log lines: $logPath")
            if (Test-Path -LiteralPath $logPath) {
                foreach ($line in @(Get-Content -LiteralPath $logPath -Tail 40)) {
                    $lines.Add([string]$line)
                }
            } else {
                $lines.Add("  <missing>")
            }
        }
        $lines | Set-Content -Encoding UTF8 $path
        Write-Host "RetroArch failure forensics saved to $path"
        $lines | ForEach-Object { Write-Host $_ }
    } catch {
        Write-Warning "Unable to collect RetroArch failure forensics for $Name without changing the original failure: $($_.Exception.Message)"
    }
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
game_specific_options = "true"
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
    $runStartUtc = [DateTime]::UtcNow
    Wait-RetroArchStartReady
    $process = Start-Process -FilePath $retroArch -ArgumentList $arguments `
        -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $script:RetroArchStartedProcesses.Add($process)
    $output = @()
    if (Test-Path $stdout) { $output += Get-Content $stdout }
    if (Test-Path $stderr) { $output += Get-Content $stderr }
    $output | Tee-Object -FilePath $log -Append | Write-Host
    if ($process.ExitCode -ne 0) {
        Write-RetroArchFailureEvidence "invoke-arcdroid-$runId" $process `
            $runStartUtc @($log, $stdout, $stderr) `
            "Invoke-ArcDroid expected exit code 0"
        Remove-Item -Force -ErrorAction SilentlyContinue $stdout, $stderr
        throw "RetroArch exited with code $($process.ExitCode)"
    }
    Remove-Item -Force -ErrorAction SilentlyContinue $stdout, $stderr
}

if (-not $GuardedJfdOnly) { Invoke-ArcDroid 600 $true }

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
    $runStartUtc = [DateTime]::UtcNow
    Wait-RetroArchStartReady
    $process = Start-Process -FilePath $retroArch -ArgumentList $arguments `
        -PassThru -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $script:RetroArchStartedProcesses.Add($process)
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
        if ($process.HasExited) {
            Write-RetroArchFailureEvidence "network-reload" $process `
                $runStartUtc @($attemptLog, $stdout, $stderr) `
                "in-session reload exited unexpectedly"
        }
        return $false
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
        Remove-Item -Force -ErrorAction SilentlyContinue $stdout, $stderr
    }
}

Add-Type -AssemblyName System.Drawing
if (-not $GuardedJfdOnly) {
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
# Slice 24: RetroArch's saved game options replace the core-wide option file.
# Both fixtures retain a retired key while only the current Pad Mapping keys
# influence the native core's input resolution.
$slice24GameOptions = Join-Path $coreOptionsDirectory 'arcdroid-test.opt'
$slice24SavedCoreOptions = Get-Content -Raw -LiteralPath $coreOptions
if (Test-Path -LiteralPath $slice24GameOptions) {
    throw 'Slice 24 option fixture would overwrite existing game options'
}
try {
    @"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_input_profile = "$('el' + 'ite')"
arcdroid_pad1_up = "s"
arcdroid_pad1_down = "x"
"@ | Set-Content -Encoding ASCII $coreOptions
    $beforeLines = @(Get-Content $log).Count
    Invoke-ArcDroid 90 $false
    $globalInputLines = @(Get-Content $log | Select-Object -Skip $beforeLines)
    $globalResolution = @($globalInputLines | Where-Object { $_.Contains('Input resolution:') })
    $expectedGlobalResolution = 'Input resolution: mapping=custom p1.Up=S[option] p1.Down=X[option] p1.Left=Cursor Left[default] p1.Right=Cursor Right[default] p1.A=Space[default] p1.B=Return[default] p1.X=Left Shift[default] p1.Y=Left Ctrl[default] p1.Start=Escape[default] p1.Select=Tab[default] p2.Up=Cursor Up[default] p2.Down=Cursor Down[default] p2.Left=Cursor Left[default] p2.Right=Cursor Right[default] p2.A=Space[default] p2.B=Return[default] p2.X=Left Shift[default] p2.Y=Left Ctrl[default] p2.Start=Escape[default] p2.Select=Tab[default]'
    if ($globalResolution.Count -ne 1 -or
        $globalResolution[0].Substring($globalResolution[0].IndexOf('Input resolution:')) -cne $expectedGlobalResolution -or
        $globalResolution[0].Contains('profile=')) {
        throw "Slice 24 global option resolution failed: $($globalResolution -join ' | ')"
    }
    @"
arcdroid_input_profile = "Generic"
arcdroid_pad1_right = "period"
"@ | Set-Content -Encoding ASCII $slice24GameOptions
    $beforeLines = @(Get-Content $log).Count
    Invoke-ArcDroid 90 $false
    $gameInputLines = @(Get-Content $log | Select-Object -Skip $beforeLines)
    $gameResolution = @($gameInputLines | Where-Object { $_.Contains('Input resolution:') })
    $expectedGameResolution = 'Input resolution: mapping=custom p1.Up=Cursor Up[default] p1.Down=Cursor Down[default] p1.Left=Cursor Left[default] p1.Right=.[option] p1.A=Space[default] p1.B=Return[default] p1.X=Left Shift[default] p1.Y=Left Ctrl[default] p1.Start=Escape[default] p1.Select=Tab[default] p2.Up=Cursor Up[default] p2.Down=Cursor Down[default] p2.Left=Cursor Left[default] p2.Right=Cursor Right[default] p2.A=Space[default] p2.B=Return[default] p2.X=Left Shift[default] p2.Y=Left Ctrl[default] p2.Start=Escape[default] p2.Select=Tab[default]'
    if ($gameResolution.Count -ne 1 -or
        $gameResolution[0].Substring($gameResolution[0].IndexOf('Input resolution:')) -cne $expectedGameResolution -or
        $gameResolution[0].Contains('profile=')) {
        throw "Slice 24 game option precedence failed: $($gameResolution -join ' | ')"
    }
    $invalidRetired = @($globalInputLines + $gameInputLines | Where-Object {
        $_ -match 'arcdroid_input_profile' -and
        $_ -match '(?i)invalid|unknown|unrecognized|not found|unsupported'
    })
    if ($invalidRetired.Count) {
        throw "Slice 24 retired option produced a frontend diagnostic: $($invalidRetired -join ' | ')"
    }
    Write-Host 'Slice 24 Windows RetroArch: global and game option precedence passed'
    if ($env:GITHUB_STEP_SUMMARY) {
        Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY '- Slice 24 Pad Mapping: stale core-wide and per-game option keys were ignored; game options replaced the global Pad 1 overrides.'
    }
} finally {
    $slice24SavedCoreOptions | Set-Content -Encoding ASCII $coreOptions
    Remove-Item -Force -ErrorAction SilentlyContinue $slice24GameOptions
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
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 4 New-IO: A3010/ARM250 launch logged and the executed-branch screenshot differed from A3000."
}

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
$sparseJfd = (Resolve-Path (Join-Path $InputDirectory "sparse-d-timed.jfd")).Path
$zipSingle = (Resolve-Path (Join-Path $InputDirectory "zip-content\single.zip")).Path
$zipMulti = (Resolve-Path (Join-Path $InputDirectory "zip-content\multi.zip")).Path
$zipNameSelected = (Resolve-Path (Join-Path $InputDirectory "zip-content\Title.zip")).Path
$zipDisc1Selected = (Resolve-Path (Join-Path $InputDirectory "zip-content\DiscRule.zip")).Path
$zipSingleHash = (Get-FileHash -Algorithm SHA256 $zipSingle).Hash.ToLowerInvariant()
$zipMultiHash = (Get-FileHash -Algorithm SHA256 $zipMulti).Hash.ToLowerInvariant()
$zipNameSelectedHash = (Get-FileHash -Algorithm SHA256 $zipNameSelected).Hash.ToLowerInvariant()
$zipDisc1SelectedHash = (Get-FileHash -Algorithm SHA256 $zipDisc1Selected).Hash.ToLowerInvariant()

function Start-MediaRun([string]$MediaPath, [string]$Name) {
    $stdout = Join-Path $WorkDirectory "$Name.stdout.log"
    $stderr = Join-Path $WorkDirectory "$Name.stderr.log"
    $retroLog = Join-Path $evidenceDirectory "$Name.log"
    Remove-Item -Force -ErrorAction SilentlyContinue $retroLog
    $arguments = @("--config=$config", "--libretro=$core", "--verbose",
        "--log-file=$retroLog", $MediaPath)
    $runStartUtc = [DateTime]::UtcNow
    Wait-RetroArchStartReady
    $process = Start-Process -FilePath $retroArch -ArgumentList $arguments `
        -PassThru -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $script:RetroArchStartedProcesses.Add($process)
    $udp = [System.Net.Sockets.UdpClient]::new()
    $udp.Client.ReceiveTimeout = 500
    $udp.Connect("127.0.0.1", 55355)
    $run = @{ Process = $process; Udp = $udp; Stdout = $stdout; Stderr = $stderr;
        RetroLog = $retroLog; Name = $Name; StartUtc = $runStartUtc;
        FailureEvidenceWritten = $false; FailureReported = $false }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $ready = $false
    while ([DateTime]::UtcNow -lt $deadline -and -not $ready) {
        if ($process.HasExited) {
            Write-MediaRunFailureEvidence $run `
                "exited before its command port became ready"
            $run["FailureReported"] = $true
            $udp.Dispose()
            throw "$Name exited before its command port became ready with code $($process.ExitCode)"
        }
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
    return $run
}

function Write-MediaRunFailureEvidence($Run, [string]$Context) {
    if ($Run["FailureEvidenceWritten"]) { return }
    $Run["FailureEvidenceWritten"] = $true
    Write-RetroArchFailureEvidence $Run.Name $Run.Process $Run.StartUtc `
        @($Run.RetroLog, $Run.Stdout, $Run.Stderr) $Context
    # Also reached when the RPC process crashes before Start-MediaRun returns.
    if ($Run.Name -like 'rpc-hd-session-*') { Save-RpcHdCrashEvidence $Run }
}

function Send-MediaCommand($Run, [string]$Command) {
    if ($Run.Process.HasExited) {
        Write-MediaRunFailureEvidence $Run "exited before network command $Command"
        $Run["FailureReported"] = $true
        throw "$($Run.Name) exited before $Command with code $($Run.Process.ExitCode)"
    }
    $bytes = [Text.Encoding]::ASCII.GetBytes($Command)
    [void]$Run.Udp.Send($bytes, $bytes.Length)
}

function Stop-MediaRun($Run) {
    $unexpectedAtEntry = $Run.Process.HasExited
    try {
        if (-not $Run.Process.HasExited) {
            Send-QuitPair $Run.Udp
            if (-not $Run.Process.WaitForExit(30000)) {
                throw "$($Run.Name) did not exit within 30 seconds after QUIT"
            }
            $Run.Process.WaitForExit()
        }
        if ($unexpectedAtEntry -or $Run.Process.ExitCode -ne 0) {
            $context = if ($unexpectedAtEntry) {
                "process had already exited before Stop-MediaRun requested QUIT"
            } else {
                "Stop-MediaRun expected exit code 0 after QUIT"
            }
            Write-MediaRunFailureEvidence $Run $context
            if (-not $Run["FailureReported"]) {
                throw "$($Run.Name) exited unexpectedly with code $($Run.Process.ExitCode)"
            }
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
        if ($Run.Process.HasExited) {
            Write-MediaRunFailureEvidence $Run `
                "died while a SCREENSHOT request was outstanding for $Name"
            $Run["FailureReported"] = $true
            throw "$($Run.Name) died with code $($Run.Process.ExitCode) while the $Name SCREENSHOT request was outstanding"
        }
        $shot = Get-ChildItem -Path $evidenceDirectory -Filter "*.png" -ErrorAction SilentlyContinue |
            Where-Object { $before -notcontains $_.FullName } |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    }
    if (-not $shot) { throw "$Name screenshot was not observed within 10 seconds" }
    $renamed = Join-Path $evidenceDirectory "$Name.png"
    Move-ItemWithSharingRetry $shot.FullName $renamed "$Name screenshot"
    return Get-MediaRowSignature $renamed $Name
}

function Capture-ExpectedSignature($Run, [string]$Name, [string]$Expected,
                                   [bool]$Different, [int]$BudgetSeconds = 10) {
    $deadline = [DateTime]::UtcNow.AddSeconds($BudgetSeconds)
    $attempt = 0
    do {
        $attempt++
        try {
            $actual = Capture-MediaSignature $Run "$Name-$attempt"
        } catch {
            $retryable = $_.Exception.Message -like '*screenshot was not observed within 10 seconds*' -or
                $_.Exception.Message -like '*did not contain a machine-decodable uniform media result row*'
            if ($BudgetSeconds -le 10 -or -not $retryable -or
                [DateTime]::UtcNow -ge $deadline) { throw }
            Start-Sleep -Milliseconds 200
            continue
        }
        $matched = $actual -eq $Expected
        if ($matched -ne $Different) { return $actual }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    $relation = if ($Different) { "different from" } else { "equal to" }
    throw "$Name did not become $relation $Expected within $BudgetSeconds seconds (last $actual)"
}

function Wait-JfdOutcome($Run, [string]$Name, [string]$Expected) {
    # The save ROM publishes exactly one of these colours after the WD1770
    # command completes, then holds it forever. Two captures rule out an
    # early frame or a screenshot from before the final result was visible.
    $outcomes = @("ff6a0000", "ff001500", "ff006a00", "ff00006a")
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    $attempt = 0
    do {
        $attempt++
        $actual = Capture-MediaSignature $Run "$Name-$attempt"
        if ($outcomes -contains $actual) {
            Start-Sleep -Milliseconds 200
            $steady = Capture-MediaSignature $Run "$Name-steady-$attempt"
            if ($steady -eq $actual) {
                if ($actual -ne $Expected) {
                    throw "$Name completed with $actual; expected $Expected"
                }
                return $actual
            }
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "$Name did not publish a steady JFD outcome $Expected within 20 seconds (last $actual)"
}

function Wait-JfdResultVideoReady($Run, [string]$Name) {
    # The allocation veto ROM has two five-index searches before it programs
    # VIDC. Start-MediaRun's two-second delay can end during the final video
    # mode change; RetroArch can then discard a SCREENSHOT network command.
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Run.Process.HasExited) {
            Write-MediaRunFailureEvidence $Run "exited before $Name video was ready"
            $Run["FailureReported"] = $true
            throw "$Name exited before its result video was ready with code $($Run.Process.ExitCode)"
        }
        if (Test-Path -LiteralPath $Run.RetroLog) {
            $logText = Get-Content -LiteralPath $Run.RetroLog -Raw -ErrorAction SilentlyContinue
            if ($logText) {
                $videoInfoAt = $logText.LastIndexOf("SET_SYSTEM_AV_INFO: 640x512")
                if ($videoInfoAt -ge 0 -and $logText.IndexOf('Found display driver: "gdi"', $videoInfoAt) -ge 0) {
                    Start-Sleep -Milliseconds 500
                    return
                }
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "$Name did not complete the result video mode within 20 seconds"
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

# Slice 28 is a separate windows-retroarch job step using the same native
# frontend, screenshot oracle and process-ownership helpers.
if ($GuardedJfdOnly) {
    Copy-Item -Force (Join-Path $InputDirectory "arcdroid_libretro_test.dll") $core
    @"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_launch = "off"
arcdroid_jfd_boot = "native"
arcdroid_disk_writes = "shadow"
"@ | Set-Content -Encoding ASCII $coreOptions
    $guardImage = (Resolve-Path (Join-Path $InputDirectory "jfd-allocation/guard-crossing.jfd")).Path
    $guardHash = (Get-FileHash -Algorithm SHA256 $guardImage).Hash
    $guardDigest = (Get-FileHash -Algorithm SHA1 $guardImage).Hash.ToLowerInvariant()
    Copy-Item -Force (Join-Path $InputDirectory "arcdroid-jfd-save-test.rom") $firmware
    $guardFirst = Start-MediaRun $guardImage "guard-first"
    try { [void](Wait-JfdOutcome $guardFirst "guard-in-place" "ff6a0000") }
    finally { Stop-MediaRun $guardFirst }
    if (-not (Select-String -Path $guardFirst.RetroLog -Pattern "\bguard=old-map\s+unavailable=1\s+tail=63(?=\s|$)" -Quiet)) {
        throw "Guarded old-map write state was not logged"
    }
    $guardSecond = Start-MediaRun $guardImage "guard-relaunch"
    try { [void](Wait-JfdOutcome $guardSecond "guard-persistent" "ff001500") }
    finally { Stop-MediaRun $guardSecond }
    $guardSlots = @(Get-ChildItem -LiteralPath $saveDirectory -File -Recurse |
        Where-Object { $_.Name -eq "$guardDigest.a" -or $_.Name -eq "$guardDigest.b" })
    if (-not $guardSlots.Count) { throw "Guarded in-place write published no store" }
    Copy-Item -Force (Join-Path $InputDirectory "arcdroid-jfd-guard-data.rom") $firmware
    $guardRefuse = Start-MediaRun $guardImage "guard-unavailable"
    try {
        Wait-JfdResultVideoReady $guardRefuse "guard-unavailable"
        [void](Wait-JfdOutcome $guardRefuse "guard-unavailable-continued-use" "ff001500")
    } finally { Stop-MediaRun $guardRefuse }
    if (-not (Select-String -Path $guardRefuse.RetroLog -Pattern "\bsector=3\s+reason=guard-unavailable(?=\s|$)" -Quiet)) {
        throw "Unavailable target refusal was not logged"
    }
    if (-not (Select-String -Path $guardRefuse.RetroLog -Pattern "\bdescriptor=2(?=\s|$)" -Quiet)) {
        throw "The guest did not complete its unrelated write after refusal"
    }
    if ((Get-FileHash -Algorithm SHA256 $guardImage).Hash -ne $guardHash) {
        throw "Guarded source JFD changed"
    }
    Write-Host "Slice 28 Windows RetroArch: guarded boot, persistent in-place write and unavailable-target refusal passed"
    if ($env:GITHUB_STEP_SUMMARY) {
        Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 28: guarded old-map boot, in-place persistence across relaunch, unavailable-target error and continued use passed."
    }
    return
}

@"
arcdroid_machine = "Auto"
arcdroid_cpu = "Auto"
arcdroid_joystick_if = "Keyboard"
arcdroid_launch = "off"
"@ | Set-Content -Encoding ASCII $coreOptions
$fContent = (Resolve-Path (Join-Path $InputDirectory "valid-f.adf")).Path
$fRun = Start-MediaRun $fContent "f-format-auto"
try {
    $fRun.Udp.Client.ReceiveTimeout = 5000
    for ($sample = 1; $sample -le 2; $sample++) {
        if ($sample -eq 2) { Start-Sleep -Seconds 2 }
        if ($fRun.Process.HasExited) { throw "F-format RetroArch exited before GET_STATUS sample $sample" }
        Send-MediaCommand $fRun "GET_STATUS"
        $fRemote = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
        $fStatus = [Text.Encoding]::ASCII.GetString($fRun.Udp.Receive([ref]$fRemote))
        if ($fRun.Process.HasExited -or $fStatus -notmatch '^GET_STATUS PLAYING\b' -or -not $fStatus.Contains("valid-f")) { throw "F-format GET_STATUS sample $sample did not report live valid-f content: $fStatus" }
    }
} finally { Stop-MediaRun $fRun }
if (-not (Select-String -Path $fRun.RetroLog -SimpleMatch "Machine: requested=Auto resolved=A3020 reason=hd-media" -Quiet)) {
    throw "F-format RetroArch run did not resolve A3020; log=$($fRun.RetroLog)"
}
if (-not (Select-String -Path $fRun.RetroLog -Pattern 'presenting bounded fallback|SET_GEOMETRY|SET_SYSTEM_AV_INFO' -Quiet)) {
    throw "F-format RetroArch run did not present frames; log=$($fRun.RetroLog)"
}
if (Select-String -Path $fRun.RetroLog -Pattern '\[(?:libretro )?ERROR\]' -Quiet) {
    throw "F-format RetroArch run logged an error; log=$($fRun.RetroLog)"
}
$fHardDiscLine = @(Select-String -Path $fRun.RetroLog -Pattern 'Hard disc: policy=auto attach=yes mode=writable reason=attached store=created generation=0 id=[0-9a-f]{40}' | Select-Object -Last 1)
if ($fHardDiscLine.Count -ne 1) {
    throw "F-format RetroArch run did not publish and attach a writable native hard disc; log=$($fRun.RetroLog)"
}
$fHardDiscId = [regex]::Match($fHardDiscLine[0].Line, 'id=([0-9a-f]{40})').Groups[1].Value
$fHardDiscStore = Join-Path $saveDirectory "ArcDroid/arcdroid/hd/$fHardDiscId"
if (-not (Test-Path (Join-Path $fHardDiscStore 'ready.a')) -or
    -not (Test-Path (Join-Path $fHardDiscStore 'journal.g0'))) {
    throw 'F-format RetroArch run did not leave a committed native hard disc'
}

# Authored native IDE guest: a blank disc paints outcome 0x31 after a sector
# write; the next RetroArch process reads that sector and paints outcome 0x42.
Copy-Item -Force (Join-Path $InputDirectory 'arcdroid_libretro_test.dll') $core
Copy-Item -Force (Join-Path $InputDirectory 'arcdroid-native-hd-guest.rom') $firmware
@"
arcdroid_machine = "Auto"
arcdroid_cpu = "Auto"
arcdroid_launch = "off"
arcdroid_disk_writes = "shadow"
arcdroid_hard_disc = "auto"
"@ | Set-Content -Encoding ASCII $coreOptions
$previousScreenshot = $screenshot
$previousContent = $content
$content = $fContent
$screenshot = Join-Path $evidenceDirectory 'native-hd-written.png'
Invoke-ArcDroid 1200 $true
$nativeWritten = Get-MediaRowSignature $screenshot 'native-hd-written'
if (-not (Select-String -Path $log -SimpleMatch 'Hard disc: journal-commit lba=15456 count=1' -Quiet)) {
    throw 'Native IDE guest did not commit its first save'
}
$screenshot = Join-Path $evidenceDirectory 'native-hd-reloaded.png'
Invoke-ArcDroid 1200 $true
$nativeReloaded = Get-MediaRowSignature $screenshot 'native-hd-reloaded'
# The guest's painted 0x42 outcome appears as ff009400 through VIDC.
if ($nativeWritten -eq $nativeReloaded -or $nativeReloaded -ne 'ff009400' -or
    -not (Select-String -Path $log -SimpleMatch 'Hard disc: store-state=ready writable' -Quiet)) {
    throw 'Native IDE guest did not show its saved sector on a fresh process'
}
$screenshot = $previousScreenshot
$content = $previousContent
Copy-Item -Force (Join-Path $InputDirectory 'arcdroid_libretro.dll') $core
Copy-Item -Force (Join-Path $InputDirectory 'arcdroid-media-test.rom') $firmware
@"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
"@ | Set-Content -Encoding ASCII $coreOptions

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

function Assert-ZipRunLog($Run, [string]$Context) {
    if (Select-String -Path $Run.RetroLog -SimpleMatch "Core requires uncompressed content" -Quiet) {
        throw "${Context}: RetroArch extracted or rejected the archive instead of passing it to ArcDroid; log=$($Run.RetroLog)"
    }
    if (-not (Select-String -Path $Run.RetroLog -SimpleMatch "Zip content:" -Quiet)) {
        $tail = (Get-Content -LiteralPath $Run.RetroLog -Tail 40) -join " | "
        throw "${Context}: ArcDroid's Zip content summary was absent; log_tail=[$tail]"
    }
}

function Assert-ZipSelectionLog($Run, [string]$Context, [string]$Rule, [string]$Boot) {
    Assert-ZipRunLog $Run $Context
    $ruleField = "rule=$Rule"
    $bootField = "boot=`"$Boot`""
    if (-not (Select-String -Path $Run.RetroLog -SimpleMatch $ruleField -Quiet)) {
        throw "${Context}: expected $ruleField in ArcDroid's zip summary; log=$($Run.RetroLog)"
    }
    if (-not (Select-String -Path $Run.RetroLog -SimpleMatch $bootField -Quiet)) {
        throw "${Context}: expected $bootField in ArcDroid's zip summary; log=$($Run.RetroLog)"
    }
}

$zipSingleRun = Start-MediaRun $zipSingle "zip-single"
try {
    [void](Capture-ExpectedSignature $zipSingleRun "zip-single-disc1" $disc1Signature $false)
} finally {
    Stop-MediaRun $zipSingleRun
}
Assert-ZipRunLog $zipSingleRun "Single-image zip"

$zipMemberRun = Start-MediaRun "$zipSingle#Disc1.adf" "zip-member"
try {
    [void](Capture-ExpectedSignature $zipMemberRun "zip-member-disc1" $disc1Signature $false)
} finally {
    Stop-MediaRun $zipMemberRun
}
Assert-ZipRunLog $zipMemberRun "zip#member"

$zipMultiRun = Start-MediaRun $zipMulti "zip-multi-disk-control"
try {
    [void](Capture-ExpectedSignature $zipMultiRun "zip-multi-disc1" $disc1Signature $false)
    Send-MediaCommand $zipMultiRun "DISK_EJECT_TOGGLE"
    [void](Capture-ExpectedSignature $zipMultiRun "zip-multi-ejected" $disc1Signature $false)
    Send-MediaCommand $zipMultiRun "DISK_NEXT"
    [void](Capture-ExpectedSignature $zipMultiRun "zip-multi-selected-disc2" $disc1Signature $false)
    Send-MediaCommand $zipMultiRun "DISK_EJECT_TOGGLE"
    [void](Capture-ExpectedSignature $zipMultiRun "zip-multi-disc2" $disc2Signature $false)
} finally {
    Stop-MediaRun $zipMultiRun
}
Assert-ZipRunLog $zipMultiRun "Multi-image zip"

$zipNameRun = Start-MediaRun $zipNameSelected "zip-name-selected"
try {
    [void](Capture-ExpectedSignature $zipNameRun "zip-name-disc1" $disc1Signature $false)
    Send-MediaCommand $zipNameRun "DISK_EJECT_TOGGLE"
    [void](Capture-ExpectedSignature $zipNameRun "zip-name-ejected" $disc1Signature $false)
    Send-MediaCommand $zipNameRun "DISK_NEXT"
    [void](Capture-ExpectedSignature $zipNameRun "zip-name-selected-disc2" $disc1Signature $false)
    Send-MediaCommand $zipNameRun "DISK_EJECT_TOGGLE"
    [void](Capture-ExpectedSignature $zipNameRun "zip-name-disc2" $disc2Signature $false)
} finally {
    Stop-MediaRun $zipNameRun
}
Assert-ZipSelectionLog $zipNameRun "Zip-name selection" "name" "Title.adf"

$zipDisc1Run = Start-MediaRun $zipDisc1Selected "zip-disc1-selected"
try {
    [void](Capture-ExpectedSignature $zipDisc1Run "zip-rule-disc1" $disc1Signature $false)
    Send-MediaCommand $zipDisc1Run "DISK_EJECT_TOGGLE"
    [void](Capture-ExpectedSignature $zipDisc1Run "zip-rule-ejected" $disc1Signature $false)
    Send-MediaCommand $zipDisc1Run "DISK_NEXT"
    [void](Capture-ExpectedSignature $zipDisc1Run "zip-rule-selected-disc2" $disc1Signature $false)
    Send-MediaCommand $zipDisc1Run "DISK_EJECT_TOGGLE"
    [void](Capture-ExpectedSignature $zipDisc1Run "zip-rule-disc2" $disc2Signature $false)
} finally {
    Stop-MediaRun $zipDisc1Run
}
Assert-ZipSelectionLog $zipDisc1Run "Explicit disc-1 selection" "disc1" "Game (Disk 1).adf"

$zipSingleHashAfter = (Get-FileHash -Algorithm SHA256 $zipSingle).Hash.ToLowerInvariant()
$zipMultiHashAfter = (Get-FileHash -Algorithm SHA256 $zipMulti).Hash.ToLowerInvariant()
$zipNameSelectedHashAfter = (Get-FileHash -Algorithm SHA256 $zipNameSelected).Hash.ToLowerInvariant()
$zipDisc1SelectedHashAfter = (Get-FileHash -Algorithm SHA256 $zipDisc1Selected).Hash.ToLowerInvariant()
if ($zipSingleHashAfter -ne $zipSingleHash) {
    throw "Single-image zip changed during native RetroArch runs: before=$zipSingleHash after=$zipSingleHashAfter path=$zipSingle"
}
if ($zipMultiHashAfter -ne $zipMultiHash) {
    throw "Multi-image zip changed during native RetroArch run: before=$zipMultiHash after=$zipMultiHashAfter path=$zipMulti"
}
if ($zipNameSelectedHashAfter -ne $zipNameSelectedHash) {
    throw "Zip-name fixture changed during native RetroArch run: before=$zipNameSelectedHash after=$zipNameSelectedHashAfter path=$zipNameSelected"
}
if ($zipDisc1SelectedHashAfter -ne $zipDisc1SelectedHash) {
    throw "Disc-1 fixture changed during native RetroArch run: before=$zipDisc1SelectedHash after=$zipDisc1SelectedHashAfter path=$zipDisc1Selected"
}
"single_before=$zipSingleHash single_after=$zipSingleHashAfter multi_before=$zipMultiHash multi_after=$zipMultiHashAfter name_before=$zipNameSelectedHash name_after=$zipNameSelectedHashAfter disc1_before=$zipDisc1SelectedHash disc1_after=$zipDisc1SelectedHashAfter" |
    Set-Content -Encoding ASCII (Join-Path $evidenceDirectory "zip-sha256.txt")

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
if (-not (Select-String -Path $jfdRun.RetroLog -SimpleMatch "JFD identification: image=1 native=yes format=none schedule=0 timing=timed captured=0 presented=none basis=none maps=0 reason=map-invalid" -Quiet)) {
    throw "Two-member JFD identification refusal was not logged"
}
$sparseJfdRun = Start-MediaRun $sparseJfd "jfd-identification-sparse-d"
try {
    Start-Sleep -Seconds 2
} finally {
    Stop-MediaRun $sparseJfdRun
}
if (-not (Select-String -Path $sparseJfdRun.RetroLog -SimpleMatch "JFD identification: image=1 native=yes format=D schedule=5 timing=timed captured=0,1,2 presented=3,4 basis=free maps=1 reason=presented" -Quiet)) {
    throw "Sparse D JFD identification was not logged"
}

Write-Host "Slice 2a Windows RetroArch: disk commands, image-1 relaunch, inert seeded .ldci, and JFD golden region passed"

# Slice 10a: the project-authored ROM reads the entire sector before writing
# 0x5A bytes, then publishes a steady result for that prewrite read and the
# completed write status. A relaunch must report data from the saved overlay.
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-jfd-save-test.rom") $firmware
$rwJfd = (Resolve-Path (Join-Path $InputDirectory "jfd-writes/rw-d.jfd")).Path
$wpJfd = (Resolve-Path (Join-Path $InputDirectory "jfd-writes/wp-d.jfd")).Path
$rwZip = (Resolve-Path (Join-Path $InputDirectory "jfd-writes/rw-d.zip")).Path
$rwHash = (Get-FileHash -Algorithm SHA256 $rwJfd).Hash
$wpHash = (Get-FileHash -Algorithm SHA256 $wpJfd).Hash
$zipHash = (Get-FileHash -Algorithm SHA256 $rwZip).Hash
$rwDigest = (Get-FileHash -Algorithm SHA1 $rwJfd).Hash.ToLowerInvariant()
$wpDigest = (Get-FileHash -Algorithm SHA1 $wpJfd).Hash.ToLowerInvariant()
$jfdSaveDirectory = Join-Path $coreSaveDirectory "jfd"
if ((Test-Path (Join-Path $jfdSaveDirectory "$rwDigest.a")) -or
    (Test-Path (Join-Path $jfdSaveDirectory "$rwDigest.b")) -or
    (Test-Path (Join-Path $jfdSaveDirectory "$wpDigest.a")) -or
    (Test-Path (Join-Path $jfdSaveDirectory "$wpDigest.b"))) {
    throw "JFD save slots were present before the native write test"
}
$rwFirst = Start-MediaRun $rwJfd "jfd-rw-first"
try {
    [void](Wait-JfdOutcome $rwFirst "jfd-rw-first-accepted" "ff6a0000")
} finally { Stop-MediaRun $rwFirst }
if (-not (Select-String -Path $rwFirst.RetroLog -SimpleMatch "JFD writes: image=1 state=writable reason=rw-flag" -Quiet)) {
    throw "RW JFD did not enter the native writable path"
}
if (-not ((Test-Path (Join-Path $jfdSaveDirectory "$rwDigest.a")) -or
           (Test-Path (Join-Path $jfdSaveDirectory "$rwDigest.b")))) {
    throw "RW JFD did not publish a committed save slot"
}
$rwSecond = Start-MediaRun $rwJfd "jfd-rw-relaunch"
try {
    [void](Wait-JfdOutcome $rwSecond "jfd-rw-saved-readback" "ff001500")
} finally { Stop-MediaRun $rwSecond }
$wpFirst = Start-MediaRun $wpJfd "jfd-wp-first"
try {
    [void](Wait-JfdOutcome $wpFirst "jfd-wp-refused-pristine" "ff006a00")
} finally { Stop-MediaRun $wpFirst }
if (-not (Select-String -Path $wpFirst.RetroLog -SimpleMatch "JFD writes: image=1 state=protected reason=wp-flag" -Quiet)) {
    throw "WP JFD did not remain protected"
}
if ((Test-Path (Join-Path $jfdSaveDirectory "$wpDigest.a")) -or
    (Test-Path (Join-Path $jfdSaveDirectory "$wpDigest.b"))) {
    throw "WP JFD unexpectedly published a save slot"
}
if ((Get-FileHash -Algorithm SHA256 $rwJfd).Hash -ne $rwHash -or
    (Get-FileHash -Algorithm SHA256 $wpJfd).Hash -ne $wpHash -or
    (Get-FileHash -Algorithm SHA256 $rwZip).Hash -ne $zipHash) {
    throw "JFD or zip source changed during native write tests"
}
Write-Host "Slice 10a Windows RetroArch: RW commit survived process relaunch; WP and source files unchanged"
# C-11: the guest writes map ID 0 and directory ID 2 before creating
# missing data ID 3. The second RetroArch process only reads ID 3; its
# steady saved outcome therefore proves the created record survived reload.
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-jfd-create-test.rom") $firmware
$createRw = (Resolve-Path (Join-Path $InputDirectory "jfd-writes/rw-create-d.jfd")).Path
$createWp = (Resolve-Path (Join-Path $InputDirectory "jfd-writes/wp-create-d.jfd")).Path
$createRwHash = (Get-FileHash -Algorithm SHA256 $createRw).Hash
$createWpHash = (Get-FileHash -Algorithm SHA256 $createWp).Hash
$createRwDigest = (Get-FileHash -Algorithm SHA1 $createRw).Hash.ToLowerInvariant()
$createWpDigest = (Get-FileHash -Algorithm SHA1 $createWp).Hash.ToLowerInvariant()
if ((Test-Path (Join-Path $jfdSaveDirectory "$createRwDigest.a")) -or
    (Test-Path (Join-Path $jfdSaveDirectory "$createRwDigest.b"))) {
    throw "Created JFD slot existed before the guest allocation sequence"
}
$createFirst = Start-MediaRun $createRw "jfd-create-first"
try {
    [void](Wait-JfdOutcome $createFirst "jfd-create-accepted" "ff6a0000")
} finally { Stop-MediaRun $createFirst }
if (-not ((Test-Path (Join-Path $jfdSaveDirectory "$createRwDigest.a")) -or
           (Test-Path (Join-Path $jfdSaveDirectory "$createRwDigest.b")))) {
    throw "Omitted-sector guest write did not publish a committed slot"
}
$createSecond = Start-MediaRun $createRw "jfd-create-relaunch"
try {
    [void](Wait-JfdOutcome $createSecond "jfd-create-saved-readback" "ff001500")
} finally { Stop-MediaRun $createSecond }
$createProtected = Start-MediaRun $createWp "jfd-create-wp"
try {
    [void](Wait-JfdOutcome $createProtected "jfd-create-wp-refused" "ff006a00")
} finally { Stop-MediaRun $createProtected }
if ((Test-Path (Join-Path $jfdSaveDirectory "$createWpDigest.a")) -or
    (Test-Path (Join-Path $jfdSaveDirectory "$createWpDigest.b"))) {
    throw "Write-protected omitted-sector fixture published a slot"
}
if ((Get-FileHash -Algorithm SHA256 $createRw).Hash -ne $createRwHash -or
    (Get-FileHash -Algorithm SHA256 $createWp).Hash -ne $createWpHash) {
    throw "Omitted-sector source JFD changed"
}
Write-Host "Slice 10a Windows RetroArch: map/directory/data creation survived fresh process; WP and sources unchanged"
# C-11c: the base map allocates omitted ID 3, so the ROM reads 1024 zeros
# before its first write and merges 128 bytes of 5A. Its RAM starts poisoned
# with A5 and it checks every read byte. Free omitted ID 1 still returns RNF
# before and after the save, including in a fresh process.
$mergeRw = (Resolve-Path (Join-Path $InputDirectory "jfd-writes/rw-merge-d.jfd")).Path
$mergeWp = (Resolve-Path (Join-Path $InputDirectory "jfd-writes/wp-merge-d.jfd")).Path
$mergeRwHash = (Get-FileHash -Algorithm SHA256 $mergeRw).Hash
$mergeWpHash = (Get-FileHash -Algorithm SHA256 $mergeWp).Hash
$mergeRwDigest = (Get-FileHash -Algorithm SHA1 $mergeRw).Hash.ToLowerInvariant()
$mergeWpDigest = (Get-FileHash -Algorithm SHA1 $mergeWp).Hash.ToLowerInvariant()
if ((Test-Path (Join-Path $jfdSaveDirectory "$mergeRwDigest.a")) -or
    (Test-Path (Join-Path $jfdSaveDirectory "$mergeRwDigest.b"))) {
    throw "Allocated merge slot existed before the test"
}
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-jfd-merge-missing-test.rom") $firmware
$mergeBefore = Start-MediaRun $mergeRw "jfd-merge-before-save"
try {
    [void](Wait-JfdOutcome $mergeBefore "jfd-merge-before-save-rnf" "ff00006a")
} finally { Stop-MediaRun $mergeBefore }
if ((Test-Path (Join-Path $jfdSaveDirectory "$mergeRwDigest.a")) -or
    (Test-Path (Join-Path $jfdSaveDirectory "$mergeRwDigest.b"))) {
    throw "Free-sector probe published a merge slot"
}
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-jfd-merge-test.rom") $firmware
$mergeFirst = Start-MediaRun $mergeRw "jfd-merge-first"
try {
    [void](Wait-JfdOutcome $mergeFirst "jfd-merge-zero-read-then-write" "ff6a0000")
} finally { Stop-MediaRun $mergeFirst }
if (-not ((Test-Path (Join-Path $jfdSaveDirectory "$mergeRwDigest.a")) -or
           (Test-Path (Join-Path $jfdSaveDirectory "$mergeRwDigest.b")))) {
    throw "Allocated merge did not publish a committed slot"
}
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-jfd-merge-readback-test.rom") $firmware
$mergeSecond = Start-MediaRun $mergeRw "jfd-merge-relaunch"
try {
    [void](Wait-JfdOutcome $mergeSecond "jfd-merge-partial-readback" "ff001500")
} finally { Stop-MediaRun $mergeSecond }
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-jfd-merge-missing-test.rom") $firmware
$mergeMissing = Start-MediaRun $mergeRw "jfd-merge-reload-missing"
try {
    [void](Wait-JfdOutcome $mergeMissing "jfd-merge-reload-rnf" "ff00006a")
} finally { Stop-MediaRun $mergeMissing }
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-jfd-merge-test.rom") $firmware
$mergeProtected = Start-MediaRun $mergeWp "jfd-merge-wp"
try {
    [void](Wait-JfdOutcome $mergeProtected "jfd-merge-wp-refused" "ff00006a")
} finally { Stop-MediaRun $mergeProtected }
if ((Test-Path (Join-Path $jfdSaveDirectory "$mergeWpDigest.a")) -or
    (Test-Path (Join-Path $jfdSaveDirectory "$mergeWpDigest.b"))) {
    throw "Protected allocated merge fixture published a slot"
}
if ((Get-FileHash -Algorithm SHA256 $mergeRw).Hash -ne $mergeRwHash -or
    (Get-FileHash -Algorithm SHA256 $mergeWp).Hash -ne $mergeWpHash) {
    throw "Allocated merge source JFD changed"
}
Write-Host "Slice 10a Windows RetroArch: base-allocated zero read, free-sector RNF, partial merge, fresh-process readback/RNF, WP and source checks passed"
# C-11c: the first process probes free omitted ID 3 and gets RNF. Its map
# claim is vetoed before publication; a fresh process has no probe mark, so
# the same claim commits and ID 3 reads as zero. A third process reads zero
# from the saved effective map without writing.
$allocationImage = (Resolve-Path (Join-Path $InputDirectory "jfd-allocation/old-free.jfd")).Path
$allocationHash = (Get-FileHash -Algorithm SHA256 $allocationImage).Hash
$allocationDigest = (Get-FileHash -Algorithm SHA1 $allocationImage).Hash.ToLowerInvariant()
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-jfd-allocation-veto-test.rom") $firmware
$allocationVeto = Start-MediaRun $allocationImage "jfd-allocation-veto"
try {
    Wait-JfdResultVideoReady $allocationVeto "jfd-allocation-probe-veto-reprobe"
    [void](Wait-JfdOutcome $allocationVeto "jfd-allocation-probe-veto-reprobe" "ff001500")
} finally { Stop-MediaRun $allocationVeto }
if ((Test-Path (Join-Path $jfdSaveDirectory "$allocationDigest.a")) -or
    (Test-Path (Join-Path $jfdSaveDirectory "$allocationDigest.b"))) {
    throw "Probed free-sector map claim published a slot"
}
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-jfd-allocation-claim-test.rom") $firmware
$allocationClaim = Start-MediaRun $allocationImage "jfd-allocation-claim"
try {
    Wait-JfdResultVideoReady $allocationClaim "jfd-allocation-fresh-claim-zero"
    [void](Wait-JfdOutcome $allocationClaim "jfd-allocation-fresh-claim-zero" "ff6a0000")
} finally { Stop-MediaRun $allocationClaim }
if (-not ((Test-Path (Join-Path $jfdSaveDirectory "$allocationDigest.a")) -or
           (Test-Path (Join-Path $jfdSaveDirectory "$allocationDigest.b")))) {
    throw "Fresh-process allocation map claim did not publish a slot"
}
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-jfd-allocation-read-test.rom") $firmware
$allocationRead = Start-MediaRun $allocationImage "jfd-allocation-read"
try {
    Wait-JfdResultVideoReady $allocationRead "jfd-allocation-fresh-read-zero"
    [void](Wait-JfdOutcome $allocationRead "jfd-allocation-fresh-read-zero" "ff001500")
} finally { Stop-MediaRun $allocationRead }
if ((Get-FileHash -Algorithm SHA256 $allocationImage).Hash -ne $allocationHash) {
    throw "Allocation source JFD changed"
}
Write-Host "Slice 10a Windows RetroArch: probe veto, fresh-process claim and zero read passed"
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-media-test.rom") $firmware
Write-Host "Slice 5b Windows RetroArch: folder disc 1 and eject/next/insert matched the playlist disc-2 signature"
Write-Host "Slice 7a Windows RetroArch: bare single, zip#member, multi-image swap, pass-through logs, and unchanged SHA-256 passed"
if ($env:GITHUB_STEP_SUMMARY) {
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 2a media: playlist disc 1 observed; eject/next/close observed before successive commands; disc 2 region observed."
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 5b-2 boot image: swap, clean quit and relaunch started on disc 1 without creating an .ldci; a seeded valid disc-2 record was ignored without an initial-disc error and remained byte-identical."
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- JFD: generated fixture reached the disc 1 golden result region."
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 5b folder set: selected disc observed; eject/next/insert matched the playlist disc-2 signature."
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 7a zip: single and zip#member matched unzipped disc 1; multi-image swap matched disc 2; archives remained byte-identical."
}

# Slice 2b uses the host-test core because the generated firmware is the
# project-authored test ROM, not one of the four shipping RISC OS 3 classes.
# The shipping DLL above has already completed all native runs and guards.
Copy-Item -Force (Join-Path $InputDirectory "arcdroid_libretro_test.dll") $core
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-test.rom") $firmware
$adffsDirectory = Join-Path (Split-Path $systemDirectory) "adffs"
New-Item -ItemType Directory -Force $adffsDirectory | Out-Null
Copy-Item -Force (Join-Path $InputDirectory "synthetic.zip") `
    (Join-Path $adffsDirectory "synthetic.zip")
$content = (Resolve-Path (Join-Path $InputDirectory "eligible.jfd")).Path
Invoke-ArcDroid 100 $false
$adffsLines = Get-Content $log | Select-String -SimpleMatch "JFD boot: path=adffs"
if (-not ($adffsLines.Line -match 'script=builtin profile=synthetic-test version="Synthetic 1.0"')) {
    throw "Slice 6a synthetic JFD did not select the ADFFS path through RetroArch VFS"
}
Write-Host "Slice 6a Windows RetroArch: synthetic package selected through frontend VFS"
if ($env:GITHUB_STEP_SUMMARY) {
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 6a ADFFS: generated package and JFD selected path=adffs through RetroArch's frontend VFS."
}

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
        if ($Run.Process.HasExited) {
            Write-MediaRunFailureEvidence $Run `
                "died while a SCREENSHOT request was outstanding for $Name"
            $Run["FailureReported"] = $true
            throw "$($Run.Name) died with code $($Run.Process.ExitCode) while the $Name SCREENSHOT request was outstanding"
        }
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
    Move-ItemWithSharingRetry $shot.FullName $renamed "$Name screenshot"
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
        $chooserRow2Pixel = "{0:x6}" -f
            ($image.GetPixel(60, 120).ToArgb() -band 0x00ffffff)
        $chooserRow3Pixel = "{0:x6}" -f
            ($image.GetPixel(60, 140).ToArgb() -band 0x00ffffff)
    } finally {
        if ($pixelHasher) { $pixelHasher.Dispose() }
        if ($bitmapData) { $canonical.UnlockBits($bitmapData) }
        if ($graphics) { $graphics.Dispose() }
        if ($canonical) { $canonical.Dispose() }
        $image.Dispose()
    }
    $hash = (Get-FileHash -Algorithm SHA256 $renamed).Hash.ToLowerInvariant()
    "$Name path=$renamed dimensions=$dimensions sampled_colours=$($sampledColours.Count) background_rgb=$backgroundPixel chooser_row1_rgb=$chooserFocusPixel chooser_row2_rgb=$chooserRow2Pixel chooser_row3_rgb=$chooserRow3Pixel visual_signature=$visualSignature png_sha256=$hash" |
        Add-Content -Encoding ASCII (Join-Path $evidenceDirectory "launch-chooser-signatures.txt")
    return @{ Path = $renamed; Hash = $hash; Dimensions = $dimensions;
        Colours = $sampledColours.Count; BackgroundPixel = $backgroundPixel;
        ChooserFocusPixel = $chooserFocusPixel; ChooserRow2Pixel = $chooserRow2Pixel;
        ChooserRow3Pixel = $chooserRow3Pixel; VisualSignature = $visualSignature }
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

Add-Type @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class ArcDroidWindowInput {
    private const uint INPUT_KEYBOARD = 1;
    private const uint KEYEVENTF_EXTENDEDKEY = 0x0001;
    private const uint KEYEVENTF_KEYUP = 0x0002;
    private const uint KEYEVENTF_SCANCODE = 0x0008;

    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    public struct KEYBDINPUT {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public UIntPtr dwExtraInfo;
    }

    // RetroArch-Win64 and this runner are x64. Native INPUT is 40 bytes on
    // x64: the four-byte type is followed by four bytes of alignment, then
    // the union at offset 8. Its size is set by 32-byte MOUSEINPUT even when
    // the KEYBDINPUT (24 bytes) member is used.
    [StructLayout(LayoutKind.Explicit, Size = 40)]
    public struct INPUT {
        [FieldOffset(0)] public uint type;
        [FieldOffset(8)] public KEYBDINPUT ki;
    }

    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr window);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool AttachThreadInput(uint attach, uint attachTo, bool attachInput);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint count, INPUT[] inputs, int size);

    public static int InputSize() { return Marshal.SizeOf(typeof(INPUT)); }
    public static int KeyboardInputSize() { return Marshal.SizeOf(typeof(KEYBDINPUT)); }
    public static int KeyboardUnionOffset() {
        return Marshal.OffsetOf(typeof(INPUT), "ki").ToInt32();
    }

    public static void SendScanCode(ushort scanCode, bool extended, bool keyUp) {
        if (IntPtr.Size != 8 || InputSize() != 40 ||
            KeyboardInputSize() != 24 || KeyboardUnionOffset() != 8)
            throw new InvalidOperationException(String.Format(
                "Unexpected Win32 input layout: pointer={0} INPUT={1} KEYBDINPUT={2} union_offset={3}",
                IntPtr.Size, InputSize(), KeyboardInputSize(), KeyboardUnionOffset()));
        INPUT input = new INPUT();
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = 0;
        input.ki.wScan = scanCode;
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        if (extended) input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        if (keyUp) input.ki.dwFlags |= KEYEVENTF_KEYUP;
        uint sent = SendInput(1, new INPUT[] { input }, InputSize());
        if (sent != 1)
            throw new Win32Exception(Marshal.GetLastWin32Error(), String.Format(
                "SendInput inserted {0} of 1 scan-code events", sent));
    }
}
"@

$chooserInputDiagnostics = Join-Path $evidenceDirectory "launch-chooser-input.txt"
Remove-Item -Force -ErrorAction SilentlyContinue $chooserInputDiagnostics
function Write-ChooserInputDiagnostic([string]$Message) {
    $line = "$(Get-Date -Format o) $Message"
    Write-Host $line
    $line | Add-Content -Encoding ASCII $chooserInputDiagnostics
}

$inputSize = [ArcDroidWindowInput]::InputSize()
$keyboardInputSize = [ArcDroidWindowInput]::KeyboardInputSize()
$keyboardUnionOffset = [ArcDroidWindowInput]::KeyboardUnionOffset()
Write-ChooserInputDiagnostic "input_layout pointer_bytes=$([IntPtr]::Size) input_bytes=$inputSize keybdinput_bytes=$keyboardInputSize keyboard_union_offset=$keyboardUnionOffset"
if ([IntPtr]::Size -ne 8 -or $inputSize -ne 40 -or
    $keyboardInputSize -ne 24 -or $keyboardUnionOffset -ne 8) {
    throw "Unexpected Win32 input layout before keyboard injection: pointer=$([IntPtr]::Size) INPUT=$inputSize KEYBDINPUT=$keyboardInputSize union_offset=$keyboardUnionOffset; diagnostics=$chooserInputDiagnostics"
}

function Format-WindowHandle([IntPtr]$Handle) {
    return ("0x{0:x}" -f $Handle.ToInt64())
}

function Send-HardwareScanCode([UInt16]$ScanCode, [bool]$Extended,
                               [string]$Name, [int]$HoldMilliseconds) {
    Write-ChooserInputDiagnostic "$Name key-down scan_code=0x$($ScanCode.ToString('x2')) extended=$Extended hold_ms=$HoldMilliseconds"
    [ArcDroidWindowInput]::SendScanCode($ScanCode, $Extended, $false)
    try {
        Start-Sleep -Milliseconds $HoldMilliseconds
    } finally {
        [ArcDroidWindowInput]::SendScanCode($ScanCode, $Extended, $true)
        Write-ChooserInputDiagnostic "$Name key-up scan_code=0x$($ScanCode.ToString('x2')) extended=$Extended"
    }
}

function Set-ChooserWindowForeground($Run) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $handle = [IntPtr]::Zero
    while ([DateTime]::UtcNow -lt $deadline -and $handle -eq [IntPtr]::Zero) {
        if ($Run.Process.HasExited) {
            Write-MediaRunFailureEvidence $Run `
                "died while acquiring the window for keyboard injection"
            $Run["FailureReported"] = $true
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
    $setResult = [ArcDroidWindowInput]::SetForegroundWindow($handle)
    Start-Sleep -Milliseconds 250
    $foreground = [ArcDroidWindowInput]::GetForegroundWindow()
    Write-ChooserInputDiagnostic "focus direct_set=$setResult target=$(Format-WindowHandle $handle) foreground=$(Format-WindowHandle $foreground) process_pid=$($Run.Process.Id)"
    if ($foreground -ne $handle) {
        # Windows can refuse SetForegroundWindow even when it returns success.
        # A real Alt tap marks this process as the most recent input source,
        # after which another foreground request is normally allowed.
        Send-HardwareScanCode 0x38 $false "Alt-focus-workaround" 50
        $setResult = [ArcDroidWindowInput]::SetForegroundWindow($handle)
        Start-Sleep -Milliseconds 250
        $foreground = [ArcDroidWindowInput]::GetForegroundWindow()
        Write-ChooserInputDiagnostic "focus after_alt_set=$setResult target=$(Format-WindowHandle $handle) foreground=$(Format-WindowHandle $foreground)"
    }
    if ($foreground -ne $handle) {
        # If the runner still owns foreground activation elsewhere, temporarily
        # share that thread's input queue (and the target's queue) while moving
        # focus. Always detach the queues before injecting the chooser keys.
        [uint32]$foregroundPid = 0
        [uint32]$targetPid = 0
        $currentThread = [ArcDroidWindowInput]::GetCurrentThreadId()
        $foregroundThread = if ($foreground -ne [IntPtr]::Zero) {
            [ArcDroidWindowInput]::GetWindowThreadProcessId($foreground, [ref]$foregroundPid)
        } else { 0 }
        $targetThread = [ArcDroidWindowInput]::GetWindowThreadProcessId($handle, [ref]$targetPid)
        $attachedForeground = $false
        $attachedTarget = $false
        try {
            if ($foregroundThread -and $foregroundThread -ne $currentThread) {
                $attachedForeground = [ArcDroidWindowInput]::AttachThreadInput(
                    $currentThread, $foregroundThread, $true)
            }
            if ($targetThread -and $targetThread -ne $currentThread -and
                $targetThread -ne $foregroundThread) {
                $attachedTarget = [ArcDroidWindowInput]::AttachThreadInput(
                    $currentThread, $targetThread, $true)
            }
            $topResult = [ArcDroidWindowInput]::BringWindowToTop($handle)
            $setResult = [ArcDroidWindowInput]::SetForegroundWindow($handle)
            $focusResult = [ArcDroidWindowInput]::SetFocus($handle)
            Write-ChooserInputDiagnostic "focus attached current_thread=$currentThread foreground_thread=$foregroundThread foreground_pid=$foregroundPid target_thread=$targetThread target_pid=$targetPid attached_foreground=$attachedForeground attached_target=$attachedTarget bring_to_top=$topResult set=$setResult set_focus=$(Format-WindowHandle $focusResult)"
        } finally {
            if ($attachedTarget) {
                [void][ArcDroidWindowInput]::AttachThreadInput(
                    $currentThread, $targetThread, $false)
            }
            if ($attachedForeground) {
                [void][ArcDroidWindowInput]::AttachThreadInput(
                    $currentThread, $foregroundThread, $false)
            }
        }
        Start-Sleep -Milliseconds 250
        $foreground = [ArcDroidWindowInput]::GetForegroundWindow()
        Write-ChooserInputDiagnostic "focus after_attach target=$(Format-WindowHandle $handle) foreground=$(Format-WindowHandle $foreground)"
    }
    if ($foreground -ne $handle) {
        throw "$($Run.Name) keyboard-injection step could not verify the RetroArch foreground window after direct, Alt-tap, and AttachThreadInput attempts; target=$(Format-WindowHandle $handle) foreground=$(Format-WindowHandle $foreground) process_pid=$($Run.Process.Id); diagnostics=$chooserInputDiagnostics; retroarch_log_tail=[$(Get-RunLogTail $Run)]"
    }
    return $handle
}

function Get-ChooserFocusState($Capture) {
    if ($Capture.Dimensions -ne "640x480") { return "wrong-dimensions" }
    if ($Capture.ChooserFocusPixel -eq "a8d8ff" -and
        $Capture.ChooserRow2Pixel -eq "101a2a") { return "row1" }
    if ($Capture.ChooserFocusPixel -eq "101a2a" -and
        $Capture.ChooserRow2Pixel -eq "a8d8ff") { return "row2" }
    if ($Capture.ChooserFocusPixel -eq "101a2a" -and
        $Capture.ChooserRow2Pixel -eq "101a2a" -and
        $Capture.ChooserRow3Pixel -eq "a8d8ff") { return "row3" }
    return "unrecognized"
}

function Move-ChooserFocusToSecondRow($Run, $InitialCapture,
                                      [IntPtr]$TargetHandle) {
    $observations = [System.Collections.Generic.List[string]]::new()
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    $totalSamples = 0
    for ($downAttempt = 1; $downAttempt -le 3 -and
            $totalSamples -lt 5 -and [DateTime]::UtcNow -lt $deadline;
            $downAttempt++) {
        $foreground = [ArcDroidWindowInput]::GetForegroundWindow()
        if ($foreground -ne $TargetHandle) {
            Write-ChooserInputDiagnostic "Down attempt=$downAttempt lost_foreground target=$(Format-WindowHandle $TargetHandle) foreground=$(Format-WindowHandle $foreground); retrying focus acquisition"
            $TargetHandle = Set-ChooserWindowForeground $Run
            $foreground = [ArcDroidWindowInput]::GetForegroundWindow()
        }
        Write-ChooserInputDiagnostic "Down attempt=$downAttempt foreground=$(Format-WindowHandle $foreground)"
        Send-HardwareScanCode 0x50 $true "Down" 150
        Start-Sleep -Milliseconds 200
        $recognized = $false
        for ($captureAttempt = 1; $totalSamples -lt 5 -and
                [DateTime]::UtcNow -lt $deadline; $captureAttempt++) {
            $capture = Capture-RawScreenshot $Run "launch-chooser-down-$downAttempt-$captureAttempt"
            $totalSamples++
            $state = Get-ChooserFocusState $capture
            $observation = "down_attempt=$downAttempt capture_attempt=$captureAttempt state=$state visual_signature=$($capture.VisualSignature) png_sha256=$($capture.Hash) dimensions=$($capture.Dimensions) row1_rgb=$($capture.ChooserFocusPixel) row2_rgb=$($capture.ChooserRow2Pixel) row3_rgb=$($capture.ChooserRow3Pixel)"
            $observations.Add($observation)
            Write-ChooserInputDiagnostic $observation
            if ($state -eq "row2" -and
                $capture.VisualSignature -ne $InitialCapture.VisualSignature) {
                return $capture
            }
            if ($state -eq "row1" -or $state -eq "row3") {
                $recognized = $true
                break
            }
            Start-Sleep -Milliseconds 150
        }
        if (-not $recognized -or $state -eq "row3") { break }
    }
    $foreground = [ArcDroidWindowInput]::GetForegroundWindow()
    throw "$($Run.Name) Down injection never moved chooser focus from row 1 to row 2 after 3 hardware scan-code attempts; foreground=$(Format-WindowHandle $foreground); observations=[$($observations -join ' | ')]; diagnostics=$chooserInputDiagnostics; retroarch_log_tail=[$(Get-RunLogTail $Run)]"
}

function Send-ChooserKeyboardPick($Run, $InitialCapture) {
    $handle = Set-ChooserWindowForeground $Run
    [void](Move-ChooserFocusToSecondRow $Run $InitialCapture $handle)
    Start-Sleep -Milliseconds 250
    $foreground = [ArcDroidWindowInput]::GetForegroundWindow()
    if ($foreground -ne $handle) {
        Write-ChooserInputDiagnostic "Enter lost_foreground target=$(Format-WindowHandle $handle) foreground=$(Format-WindowHandle $foreground); retrying focus acquisition"
        $handle = Set-ChooserWindowForeground $Run
        $foreground = [ArcDroidWindowInput]::GetForegroundWindow()
    }
    Write-ChooserInputDiagnostic "Enter foreground=$(Format-WindowHandle $foreground)"
    Send-HardwareScanCode 0x1c $false "Enter" 150

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Run.Process.HasExited) {
            Write-MediaRunFailureEvidence $Run `
                "died after Enter injection while awaiting the chooser decision"
            $Run["FailureReported"] = $true
            throw "$($Run.Name) exited after Enter injection code=$($Run.Process.ExitCode); diagnostics=$chooserInputDiagnostics; retroarch_log_tail=[$(Get-RunLogTail $Run)]"
        }
        if ((Test-Path -LiteralPath $Run.RetroLog) -and
            (Select-String -Path $Run.RetroLog -SimpleMatch "chooser chose target" -Quiet)) {
            Write-ChooserInputDiagnostic "Enter observed chooser chose target"
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "$($Run.Name) Enter injection did not produce 'chooser chose target' within 10 seconds; foreground=$(Format-WindowHandle ([ArcDroidWindowInput]::GetForegroundWindow())); diagnostics=$chooserInputDiagnostics; retroarch_log_tail=[$(Get-RunLogTail $Run)]"
}

function Wait-ChooserPostReinitQuiet($Run) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $transitionObservedUtc = $null
    $transitionKind = $null
    Write-ChooserInputDiagnostic "post-choice quiet wait started; no SCREENSHOT request outstanding"
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Run.Process.HasExited) {
            Write-MediaRunFailureEvidence $Run `
                "died during the post-choice video-driver quiet wait with NO screenshot request outstanding"
            $Run["FailureReported"] = $true
            throw "$($Run.Name) died with code $($Run.Process.ExitCode) during the post-choice video-driver quiet wait with NO screenshot request outstanding"
        }

        $lines = if (Test-Path -LiteralPath $Run.RetroLog) {
            @(Get-Content -LiteralPath $Run.RetroLog)
        } else { @() }
        $choiceIndex = -1
        for ($index = $lines.Count - 1; $index -ge 0; --$index) {
            if ($lines[$index].Contains("chooser chose target")) {
                $choiceIndex = $index
                break
            }
        }
        if ($choiceIndex -ge 0) {
            $postChoice = if ($choiceIndex + 1 -lt $lines.Count) {
                @($lines[($choiceIndex + 1)..($lines.Count - 1)])
            } else { @() }
            $hasAvInfo = @($postChoice | Where-Object {
                $_.Contains("SET_SYSTEM_AV_INFO")
            }).Count -gt 0
            $hasGeometry = @($postChoice | Where-Object {
                $_.Contains("SET_GEOMETRY")
            }).Count -gt 0
            $hasGdiDisplay = @($postChoice | Where-Object {
                $_.Contains('Found display driver: "gdi"')
            }).Count -gt 0
            $hasMidiInput = @($postChoice | Where-Object {
                $_.Contains('[MIDI] Input device:')
            }).Count -gt 0
            $hasMidiOutput = @($postChoice | Where-Object {
                $_.Contains('[MIDI] Output device:')
            }).Count -gt 0
            if (-not $transitionObservedUtc -and $hasAvInfo -and $hasGdiDisplay -and
                $hasMidiInput -and $hasMidiOutput) {
                $transitionObservedUtc = [DateTime]::UtcNow
                $transitionKind = "SET_SYSTEM_AV_INFO with driver re-initialisation"
                Write-ChooserInputDiagnostic "post-choice reinit markers observed: SET_SYSTEM_AV_INFO, gdi display, MIDI input/output; continuing quiet wait for 1500 ms"
            } elseif (-not $transitionObservedUtc -and -not $hasAvInfo -and
                $hasGeometry) {
                $transitionObservedUtc = [DateTime]::UtcNow
                $transitionKind = "SET_GEOMETRY without driver re-initialisation"
                Write-ChooserInputDiagnostic "post-choice geometry marker observed: SET_GEOMETRY; continuing quiet wait for 1500 ms"
            }
        }
        if ($transitionObservedUtc -and
            ([DateTime]::UtcNow - $transitionObservedUtc).TotalMilliseconds -ge 1500) {
            if ($Run.Process.HasExited) {
                Write-MediaRunFailureEvidence $Run `
                    "died at the end of the post-choice video-driver quiet wait with NO screenshot request outstanding"
                $Run["FailureReported"] = $true
                throw "$($Run.Name) died with code $($Run.Process.ExitCode) during the post-choice video-driver quiet wait with NO screenshot request outstanding"
            }
            Write-ChooserInputDiagnostic "post-choice quiet wait complete; transition=$transitionKind process_alive=True elapsed_after_markers_ms=$([int]([DateTime]::UtcNow - $transitionObservedUtc).TotalMilliseconds)"
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "$($Run.Name) did not show either post-choice SET_GEOMETRY or SET_SYSTEM_AV_INFO/GDI/MIDI completion markers and survive the 1500 ms quiet interval within 15 seconds; no SCREENSHOT request was sent; retroarch_log_tail=[$(Get-RunLogTail $Run)]"
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
    $recognized = $false
    $recognitionDeadline = [DateTime]::UtcNow.AddSeconds(3)
    for ($sample = 1; $sample -le 5 -and
            [DateTime]::UtcNow -lt $recognitionDeadline; $sample++) {
        $chooserCapture = Capture-RawScreenshot $chooserRun "launch-chooser-visible-$sample"
        $recognized = $chooserCapture.Dimensions -eq "640x480" -and
            $chooserCapture.Colours -ge 4 -and
            $chooserCapture.BackgroundPixel -eq "101a2a" -and
            $chooserCapture.ChooserFocusPixel -eq "a8d8ff" -and
            $chooserCapture.ChooserRow2Pixel -eq "101a2a" -and
            $chooserCapture.ChooserRow3Pixel -eq "101a2a" -and
            $chooserCapture.VisualSignature -ne $app1Capture.VisualSignature -and
            $chooserCapture.VisualSignature -ne $app2Capture.VisualSignature
        if ($recognized) { break }
        Start-Sleep -Milliseconds 150
    }
    if (-not $recognized) {
        throw "Chooser-recognition step expected the 640x480 ArcDroid renderer with >=4 sampled colours, background_rgb=101a2a, row1_rgb=a8d8ff, row2_rgb=101a2a and row3_rgb=101a2a; observed dimensions=$($chooserCapture.Dimensions) sampled_colours=$($chooserCapture.Colours) background_rgb=$($chooserCapture.BackgroundPixel) row1_rgb=$($chooserCapture.ChooserFocusPixel) row2_rgb=$($chooserCapture.ChooserRow2Pixel) row3_rgb=$($chooserCapture.ChooserRow3Pixel) visual_signature=$($chooserCapture.VisualSignature) png_sha256=$($chooserCapture.Hash); app1=$($app1Capture.VisualSignature) app2=$($app2Capture.VisualSignature); retroarch_log_tail=[$(Get-RunLogTail $chooserRun)]"
    }
    Send-ChooserKeyboardPick $chooserRun $chooserCapture
    Wait-ChooserPostReinitQuiet $chooserRun
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
Write-Host "Slice 5c Windows RetroArch: chooser screenshot, row-2 focus after held scan-code Down, scan-code Enter pick, guest signature and remembered replay passed"
if ($env:GITHUB_STEP_SUMMARY) {
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 5c chooser: distinct 640x480 chooser observed; held hardware scan-code Down visibly moved focus to row 2 before scan-code Enter selected !Qaventra; relaunch replayed the same guest screenshot without the chooser."
}

# Slice 12a decision probe. The project ROM confirms helper transport and
# executable delivery; FileSwitch command semantics are covered privately.
$g1Content = (Resolve-Path (Join-Path $InputDirectory "arcdroid-g1-chooser-l.adl")).Path
$nestedLiteral = (Resolve-Path (Join-Path $InputDirectory "arcdroid-nested-chooser-l.adl")).Path
$nestedExtended = (Resolve-Path (Join-Path $InputDirectory "arcdroid-extended-chooser-d.adf")).Path
@"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_disk_writes = "off"
arcdroid_launch = "auto"
arcdroid_launch_chooser = "remember"
"@ | Set-Content -Encoding ASCII $coreOptions
$g1Run = Start-MediaRun $g1Content "launch-12a-self-boot-recovery"
try {
    [void](Capture-ExpectedVisualSignature $g1Run "launch-12a-self-boot-delivery" $app1Capture)
} finally { Stop-MediaRun $g1Run }
if (-not (Select-String -Path $g1Run.RetroLog -SimpleMatch "root-boot=no" -Quiet) -or
    -not (Select-String -Path $g1Run.RetroLog -SimpleMatch "decision=!Nulvane" -Quiet) -or
    -not (Select-String -Path $g1Run.RetroLog -SimpleMatch "reason=single application" -Quiet)) {
    throw "Slice 12a G1 decision probe did not recover the application"
}
foreach ($case in @(
    @{ Path = $nestedLiteral; Name = "literal"; Decision = "Folder.!Nulvane"; Address = "Folder.!Nulvane" },
    @{ Path = $nestedExtended; Name = "extended-literal"; Decision = 'Arc\xA0Games.!Nulvane'; Address = 'Arc\xA0Games.!Nulvane' }
)) {
    if ($case.Name -eq "extended-literal") {
        Copy-Item -Force (Join-Path $InputDirectory "arcdroid-chooser-d-test.rom") $firmware
    }
    $contentHash = (Get-FileHash -Algorithm SHA1 $case.Path).Hash.ToLowerInvariant()
    $binding = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "$contentHash.bind" -File -Recurse -ErrorAction SilentlyContinue)
    if ($binding.Count) { Remove-Item -Force $binding.FullName }
    @"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_disk_writes = "off"
arcdroid_launch = "app1"
arcdroid_launch_chooser = "remember"
"@ | Set-Content -Encoding ASCII $coreOptions
    $run = Start-MediaRun $case.Path "launch-12a-$($case.Name)-explicit"
    try {
        if ($case.Name -eq "extended-literal") {
            $extendedApp1 = Capture-StableRawScreenshot $run "launch-12a-extended-app1"
        } else {
            [void](Capture-ExpectedVisualSignature $run "launch-12a-$($case.Name)-delivery" $app1Capture)
        }
    } finally { Stop-MediaRun $run }
    $launchText = Get-Content -LiteralPath $run.RetroLog -Raw
    if (-not $launchText.Contains("decision=$($case.Decision)") -or
        -not $launchText.Contains("address=$($case.Address)") -or
        -not $launchText.Contains("scope=nested")) {
        throw "Slice 12a $($case.Name) decision/address probe failed"
    }
    if ($case.Name -eq "extended-literal") {
        $binding = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "$contentHash.bind" -File -Recurse -ErrorAction SilentlyContinue)
        if ($binding.Count) { Remove-Item -Force $binding.FullName }
        @"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_disk_writes = "off"
arcdroid_launch = "app2"
arcdroid_launch_chooser = "remember"
"@ | Set-Content -Encoding ASCII $coreOptions
        $app2Run = Start-MediaRun $case.Path "launch-12a-extended-app2-explicit"
        try {
            $extendedApp2 = Capture-StableRawScreenshot $app2Run "launch-12a-extended-app2"
        } finally { Stop-MediaRun $app2Run }
        if ($extendedApp1.Dimensions -ne $app1Capture.Dimensions -or
            $extendedApp2.Dimensions -ne $extendedApp1.Dimensions -or
            $extendedApp1.VisualSignature -eq $extendedApp2.VisualSignature) {
            throw "Slice 12a extended-byte helper did not select distinct visual targets"
        }
        $app2Text = Get-Content -LiteralPath $app2Run.RetroLog -Raw
        if (-not $app2Text.Contains('decision=Arc\xA0Games.!Qaventra') -or
            -not $app2Text.Contains('address=Arc\xA0Games.!Qaventra')) {
            throw "Slice 12a extended-byte second target probe failed"
        }
    }
}
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-chooser-test.rom") $firmware

$literalHash = (Get-FileHash -Algorithm SHA1 $nestedLiteral).Hash.ToLowerInvariant()
$literalChoice = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "$literalHash.choice" -File -Recurse -ErrorAction SilentlyContinue)
if ($literalChoice.Count) { Remove-Item -Force $literalChoice.FullName }
@"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_disk_writes = "off"
arcdroid_launch = "auto"
arcdroid_launch_chooser = "remember"
"@ | Set-Content -Encoding ASCII $coreOptions
$nestedPick = Start-MediaRun $nestedLiteral "launch-12a-nested-pick"
try {
    $nestedChooser = Capture-RawScreenshot $nestedPick "launch-12a-nested-chooser"
    if ($nestedChooser.Dimensions -ne "640x480") {
        throw "Slice 12a nested chooser was not displayed"
    }
    Send-ChooserKeyboardPick $nestedPick $nestedChooser
    Wait-ChooserPostReinitQuiet $nestedPick
    [void](Capture-ExpectedVisualSignature $nestedPick "launch-12a-nested-picked" $app2Capture)
} finally { Stop-MediaRun $nestedPick }
if (-not (Select-String -Path $nestedPick.RetroLog -SimpleMatch "chooser chose target" -Quiet) -or
    -not (Select-String -Path $nestedPick.RetroLog -SimpleMatch "decision=Folder.!Qaventra" -Quiet)) {
    throw "Slice 12a nested interactive pick did not choose the second application"
}
$nestedReplay = Start-MediaRun $nestedLiteral "launch-12a-nested-remembered"
try {
    [void](Capture-ExpectedVisualSignature $nestedReplay "launch-12a-nested-replayed" $app2Capture)
} finally { Stop-MediaRun $nestedReplay }
if (-not (Select-String -Path $nestedReplay.RetroLog -SimpleMatch "chooser remembered target" -Quiet)) {
    throw "Slice 12a nested choice did not replay"
}
Write-Host "Slice 12a Windows RetroArch: G1, literal and extended-byte nested delivery, interactive nested pick and replay passed"

$srmRecords = @(Get-ChildItem -LiteralPath $saveDirectory -Filter "*.srm" -File -Recurse -ErrorAction Stop)
if ($srmRecords.Count -ne 0) {
    throw "ArcDroid runs unexpectedly created .srm files: $($srmRecords.FullName -join ', ')"
}
Write-Host "Slice 6b Windows RetroArch: no .srm files were created under the saves directory"
if ($env:GITHUB_STEP_SUMMARY) {
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- Slice 6b persistence: no per-game .srm files were created under the saves directory."
}

# Native IDE stress guest extends the ordinary guest's saved-sector witness.
# Global and per-game options deliberately retain obsolete valid/malformed
# install values; the remaining hard-disc and write overrides must still work.
Copy-Item -Force (Join-Path $InputDirectory 'arcdroid_libretro_test.dll') $core
$previousContent = $content
$previousScreenshot = $screenshot
$content = $fContent
@"
arcdroid_machine = "Auto"
arcdroid_cpu = "Auto"
arcdroid_disk_writes = "shadow"
arcdroid_hard_disc = "auto"
arcdroid_launch = "off"
arcdroid_install_saves = "persistent"
"@ | Set-Content -Encoding ASCII $coreOptions
$stalePerGameOptions = Join-Path $coreOptionsDirectory 'valid-f.opt'
Remove-Item -Force -ErrorAction SilentlyContinue $stalePerGameOptions
Copy-Item -Force (Join-Path $InputDirectory 'arcdroid-native-hd-guest.rom') $firmware
$screenshot = Join-Path $evidenceDirectory 'native-stress-global-valid.png'
$beforeLines = @(Get-Content $log).Count
Invoke-ArcDroid 900 $true
$globalLines = @(Get-Content $log | Select-Object -Skip $beforeLines)
$readbackSignature = Get-MediaRowSignature $screenshot 'native-stress-global-valid'
if ($readbackSignature -ne 'ff009400' -or
    -not @($globalLines | Where-Object { $_.Contains('Hard disc: store-state=ready writable') }).Count -or
    -not @($globalLines | Where-Object { $_.Contains('Machine: requested=Auto resolved=A3020 reason=hd-media') }).Count) {
    throw 'Native stress setup did not read the saved witness with stale global option'
}
@"
arcdroid_machine = "A3020"
arcdroid_cpu = "Auto"
arcdroid_disk_writes = "shadow"
arcdroid_hard_disc = "auto"
arcdroid_launch = "off"
arcdroid_install_saves = "malformed!"
"@ | Set-Content -Encoding ASCII $stalePerGameOptions
$resetRun = Start-MediaRun $content 'native-stress-stale-per-game-reset'
try {
    $attachNeedle = 'Hard disc: policy=auto attach=yes mode=writable reason=attached'
    $attachBefore = @(Select-String -Path $resetRun.RetroLog -SimpleMatch $attachNeedle).Count
    if ($attachBefore -lt 1) { throw 'Native stress hard disc was not attached before RESET' }
    Send-MediaCommand $resetRun 'RESET'
    $resetDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        if ($resetRun.Process.HasExited) { throw 'Native stress guest exited during RESET' }
        $resetLines = @(Get-Content $resetRun.RetroLog -ErrorAction SilentlyContinue)
        $attachAfter = @($resetLines | Where-Object { $_.Contains($attachNeedle) }).Count
        if ($attachAfter -gt $attachBefore) { break }
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $resetDeadline)
    if ($attachAfter -le $attachBefore) {
        throw 'Native stress hard disc did not reattach after RESET'
    }
    # The guest's saved-data witness is the rendered row, so allow video to
    # resume after the logged hard-disc reattach before judging that row.
    $captureDeadline = [DateTime]::UtcNow.AddSeconds(40)
    $captureAttempt = 0
    $resetSignature = $null
    do {
        $captureAttempt++
        try {
            $resetSignature = Capture-ExpectedSignature $resetRun `
                "native-stress-after-reset-$captureAttempt" $readbackSignature $false
            break
        } catch {
            $message = $_.Exception.Message
            $retryable = $message -like '*screenshot was not observed within 10 seconds*' -or
                $message -like '*did not become equal to*' -or
                $message -like '*did not contain a machine-decodable uniform media result row*'
            if (-not $retryable -or [DateTime]::UtcNow -ge $captureDeadline) { throw }
            Start-Sleep -Milliseconds 250
        }
    } while ([DateTime]::UtcNow -lt $captureDeadline)
    if ($resetSignature -ne $readbackSignature) {
        throw 'Stale per-game option changed the native readback after RESET'
    }
    $resetLines = @(Get-Content $resetRun.RetroLog)
    if (-not @($resetLines | Where-Object {
            $_.Contains('Machine: requested=A3020 resolved=A3020 reason=option')
        }).Count -or
        -not @($resetLines | Where-Object {
            $_.Contains('Hard disc: policy=auto attach=yes mode=writable reason=attached')
        }).Count) {
        throw 'Per-game stale option displaced the hard-disc or write override'
    }
    Stop-MediaRun $resetRun
} finally {
    if (-not $resetRun.Process.HasExited) { Stop-MediaRun $resetRun }
}

function Wait-NativeStressLog($Run, [string]$Needle, [int]$Seconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Run.Process.HasExited) { throw "$($Run.Name) exited before '$Needle'" }
        if ((Test-Path $Run.RetroLog) -and
            (Select-String -Path $Run.RetroLog -SimpleMatch $Needle -Quiet)) { return }
        Start-Sleep -Milliseconds 150
    }
    throw "$($Run.Name) did not log '$Needle' within $Seconds seconds"
}
function Stop-NativeStressRunAbrupt($Run, [string]$Reason) {
    if ($Run.Process.HasExited) { throw "$($Run.Name) exited before $Reason" }
    Stop-Process -Id $Run.Process.Id -Force
    [void]$Run.Process.WaitForExit(30000)
    $Run.Udp.Dispose()
    $output = @()
    if (Test-Path $Run.Stdout) { $output += Get-Content $Run.Stdout }
    if (Test-Path $Run.Stderr) { $output += Get-Content $Run.Stderr }
    $output | Set-Content -Encoding UTF8 (Join-Path $evidenceDirectory "$($Run.Name)-process.log")
    Remove-Item -Force -ErrorAction SilentlyContinue $Run.Stdout, $Run.Stderr
}

Copy-Item -Force (Join-Path $InputDirectory 'arcdroid-native-hd-stress.rom') $firmware
$stressRun = Start-MediaRun $content 'native-stress-before-compaction'
try {
    Wait-NativeStressLog $stressRun 'journal-commit lba=6096 count=256' 120
    $stressLines = @(Get-Content $stressRun.RetroLog)
    if (@($stressLines | Where-Object { $_.Contains('journal-commit') }).Count -ne 9 -or
        (Get-Item (Join-Path $fHardDiscStore 'journal.g0')).Length -le 1MB) {
        throw 'Native guest did not commit nine whole commands past 1 MiB'
    }
    $stressSignature = Capture-ExpectedSignature $stressRun 'native-stress-written' `
        $readbackSignature $false 40
    if ($stressSignature -ne $readbackSignature) {
        throw 'Native stress guest lost its saved-data witness'
    }
    Stop-NativeStressRunAbrupt $stressRun 'nine journal commits before the next load'
} finally {
    if (-not $stressRun.Process.HasExited) {
        Stop-NativeStressRunAbrupt $stressRun 'cleanup after failed pre-compaction check'
    }
}

Copy-Item -Force (Join-Path $InputDirectory 'arcdroid-native-hd-guest.rom') $firmware
$compactRun = Start-MediaRun $content 'native-stress-after-rename'
try {
    Wait-NativeStressLog $compactRun 'store-compaction=manifest-renamed generation=1' 120
    Wait-NativeStressLog $compactRun 'store-compaction=committed' 120
    $compactLines = @(Get-Content $compactRun.RetroLog)
    if (-not (Test-Path (Join-Path $fHardDiscStore 'ready.b'))) {
        throw 'Native store did not publish generation 1'
    }
    Stop-NativeStressRunAbrupt $compactRun 'compaction manifest rename'
} finally {
    if (-not $compactRun.Process.HasExited) {
        Stop-NativeStressRunAbrupt $compactRun 'cleanup after failed rename check'
    }
}
$screenshot = Join-Path $evidenceDirectory 'native-stress-reopened.png'
$beforeLines = @(Get-Content $log).Count
Invoke-ArcDroid 900 $true
$reopenLines = @(Get-Content $log | Select-Object -Skip $beforeLines)
$reopenSignature = Get-MediaRowSignature $screenshot 'native-stress-reopened'
if ($reopenSignature -ne $readbackSignature -or
    -not @($reopenLines | Where-Object { $_.Contains('Hard disc: store-state=ready writable') }).Count) {
    throw 'Native READY store did not reopen with the same guest-visible data'
}
$stressErrors = @($globalLines + $resetLines + $stressLines + $compactLines + $reopenLines |
    Where-Object { $_ -match '\[ERROR\]' })
if ($stressErrors.Count) { throw "Native stress logs contained errors: $($stressErrors -join ' | ')" }
Remove-Item -Force -ErrorAction SilentlyContinue $stalePerGameOptions
$content = $previousContent
$screenshot = $previousScreenshot
Write-Host 'Slice 22 Windows RetroArch: native IDE stress, stale options, readback and compaction recovery passed'
if ($env:GITHUB_STEP_SUMMARY) {
    Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY '- Slice 22 native IDE stress: nine bounded commands crossed 1 MiB; both planned terminations recovered with the same guest readback; stale global and per-game options were ignored.'
}

function Wait-BootRecovery($Run, [string]$Action, [int]$Count) {
    $deadline = [DateTime]::UtcNow.AddSeconds(25)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Run.Process.HasExited) { throw "$($Run.Name) exited during boot recovery" }
        $foundLines = if (Test-Path -LiteralPath $Run.RetroLog) {
            @(Select-String -Path $Run.RetroLog -SimpleMatch "phase=recovery boot-watch=prompt recovery=$Action")
        } else { @() }
        if ($foundLines.Count -ge $Count) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "$($Run.Name) did not reach recovery=$Action count=$Count; log=[$(Get-RunLogTail $Run)]"
}

function Wait-BootHit($Run, [int]$Count) {
    $deadline = [DateTime]::UtcNow.AddSeconds(25)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Run.Process.HasExited) { throw "$($Run.Name) exited during remembered boot" }
        $foundLines = if (Test-Path -LiteralPath $Run.RetroLog) {
            @(Select-String -Path $Run.RetroLog -SimpleMatch "reason=remembered boot observation; disc !Boot skipped")
        } else { @() }
        if ($foundLines.Count -ge $Count) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "$($Run.Name) did not use the remembered boot observation; log=[$(Get-RunLogTail $Run)]"
}

Copy-Item -Force (Join-Path $InputDirectory "arcdroid-boot-prompt.rom") $firmware
@"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_disk_writes = "off"
arcdroid_launch = "auto"
arcdroid_launch_chooser = "remember"
"@ | Set-Content -Encoding ASCII $coreOptions
$bootContent = (Resolve-Path (Join-Path $InputDirectory "boot-file-3-l.adl")).Path
$bootRun = Start-MediaRun $bootContent "slice14-boot-helper"
try {
    Wait-BootRecovery $bootRun "helper" 1
    Send-MediaCommand $bootRun "RESET"
    Wait-BootHit $bootRun 1
} finally { Stop-MediaRun $bootRun }
$bootReload = Start-MediaRun $bootContent "slice14-boot-reload"
try { Wait-BootHit $bootReload 1 }
finally { Stop-MediaRun $bootReload }

@"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_disk_writes = "off"
arcdroid_launch = "auto"
arcdroid_launch_chooser = "ask"
"@ | Set-Content -Encoding ASCII $coreOptions
$bootMultiple = (Resolve-Path (Join-Path $InputDirectory "boot-matrix-3-file-multiple.adf")).Path
$bootChooser = Start-MediaRun $bootMultiple "slice14-boot-chooser"
try {
    Wait-BootRecovery $bootChooser "chooser" 1
    $bootCapture = $null
    for ($attempt = 0; $attempt -lt 8; $attempt++) {
        $bootCapture = Capture-RawScreenshot $bootChooser "slice14-chooser-$attempt"
        if ((Get-ChooserFocusState $bootCapture) -eq "row1") { break }
        Start-Sleep -Milliseconds 200
    }
    if ((Get-ChooserFocusState $bootCapture) -ne "row1") {
        throw "Slice 14 post-boot chooser was not visible"
    }
    Send-ChooserKeyboardPick $bootChooser $bootCapture
    Wait-BootRecovery $bootChooser "helper" 1
    $pickLines = @(Get-Content $bootChooser.RetroLog | Where-Object {
        $_.Contains("phase=recovery boot-watch=prompt recovery=helper")
    })
    $pickLine = $pickLines[-1]
    $pickedDecisionMatch = [regex]::Match($pickLine, 'decision=(.*?) reason=')
    $pickedHelperMatch = [regex]::Match($pickLine, 'helper=(.*?) root-boot=')
    if (-not $pickedDecisionMatch.Success -or -not $pickedHelperMatch.Success -or
        -not $pickedHelperMatch.Groups[1].Value.EndsWith(".adl")) {
        throw "Slice 14 chooser pick did not log a mounted helper; log=[$pickLine]"
    }
    $pickedApplication = $pickedDecisionMatch.Groups[1].Value
    $pickedHelper = $pickedHelperMatch.Groups[1].Value
    if (-not (Test-Path -LiteralPath $pickedHelper -PathType Leaf)) {
        throw "Slice 14 chooser helper was not published at $pickedHelper"
    }
    Send-MediaCommand $bootChooser "RESET"
    Wait-BootHit $bootChooser 1
} finally { Stop-MediaRun $bootChooser }
@"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_disk_writes = "off"
arcdroid_launch = "auto"
arcdroid_launch_chooser = "remember"
"@ | Set-Content -Encoding ASCII $coreOptions
$bootChooserReload = Start-MediaRun $bootMultiple "slice14-boot-chooser-reload"
try {
    Wait-BootHit $bootChooserReload 1
    $reloadLines = @(Get-Content $bootChooserReload.RetroLog | Where-Object {
        $_.Contains("reason=remembered boot observation; disc !Boot skipped")
    })
    $reloadLine = $reloadLines[-1]
    $reloadDecisionMatch = [regex]::Match($reloadLine, 'decision=(.*?) reason=')
    $reloadHelperMatch = [regex]::Match($reloadLine, 'helper=(.*?) root-boot=')
    if (-not $reloadDecisionMatch.Success -or -not $reloadHelperMatch.Success -or
        $reloadDecisionMatch.Groups[1].Value -ne $pickedApplication -or
        $reloadHelperMatch.Groups[1].Value -ne $pickedHelper) {
        throw "Slice 14 chooser reload did not use the picked application and helper; log=[$reloadLine]"
    }
} finally { Stop-MediaRun $bootChooserReload }
Write-Host "Slice 14 Windows RetroArch: synthetic helper recovery, remembered reset and reload, post-boot chooser pick and remembered reload passed"

# Slice 15: the final sector is absent to the guest while the declared-size
# shadow retains its bytes. The authored ROM reads sector 3 at the boundary,
# expects record-not-found on sector 4, writes sector 3, and paints red only
# when all three commands return the expected result.
Copy-Item -Force (Join-Path $InputDirectory "arcdroid_libretro_test.dll") $core
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-short-tail-wd.rom") $firmware
@"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_disk_writes = "shadow"
arcdroid_short_tail = "missing"
arcdroid_launch = "off"
"@ | Set-Content -Encoding ASCII $coreOptions
$shortContent = (Resolve-Path (Join-Path $InputDirectory "800k-d-one-sector.adf")).Path
$shortSourceHash = (Get-FileHash -Algorithm SHA1 $shortContent).Hash.ToLowerInvariant()
$shortShadow = Join-Path $coreSaveDirectory "media/$shortSourceHash/disc.adf"
Remove-Item -Force -ErrorAction SilentlyContinue $shortShadow
$shortRun = Start-MediaRun $shortContent "slice15-short-first"
try {
    [void](Wait-JfdOutcome $shortRun "slice15-boundary-read-absent-read-write" "ff6a0000")
    $shortLog = Get-Content -LiteralPath $shortRun.RetroLog -Raw
    if (-not $shortLog.Contains("Image is missing its last 1024 bytes; they are presented as absent (unreadable) sectors.")) {
        throw "Slice 15 Missing notice was absent"
    }
} finally { Stop-MediaRun $shortRun }
if (-not (Test-Path -LiteralPath $shortShadow -PathType Leaf) -or
    (Get-Item -LiteralPath $shortShadow).Length -ne 819200) {
    throw "Slice 15 declared-size shadow was not published: $shortShadow"
}
$shortBytes = [IO.File]::ReadAllBytes($shortShadow)
for ($i = 0; $i -lt 1024; $i++) {
    if ($shortBytes[817152 + $i] -ne 0x5a -or $shortBytes[818176 + $i] -ne 0) {
        throw "Slice 15 write or hidden-tail invariant failed at byte $i"
    }
}
$shortReload = Start-MediaRun $shortContent "slice15-short-reload"
try {
    [void](Wait-JfdOutcome $shortReload "slice15-persisted-present-write" "ff6a0000")
} finally { Stop-MediaRun $shortReload }
if ((Get-FileHash -Algorithm SHA1 $shortContent).Hash.ToLowerInvariant() -ne $shortSourceHash) {
    throw "Slice 15 source changed during shadow writes"
}
Write-Host "Slice 15 Windows RetroArch: boundary read, absent-sector error, persisted present-sector write, notice and hidden-tail isolation passed"

# Slice 18: only the test DLL can opt ARC_OS_TEST into the RISC OS 3 CMOS
# policy. The authored ROM acknowledges both KART transitions and reads the
# mounted floppy. The authored ROM also writes sector 1 before programming
# video and KART, so its source image needs a private writable shadow.
# A frontend RESET must reconstruct and replay the sequence.
Copy-Item -Force (Join-Path $InputDirectory "arcdroid_libretro_test.dll") $core
Copy-Item -Force (Join-Path $InputDirectory "arcdroid-test.rom") $firmware
@"
arcdroid_machine = "A3000"
arcdroid_cpu = "ARM2"
arcdroid_joystick_if = "Keyboard"
arcdroid_disk_writes = "shadow"
arcdroid_launch = "off"
"@ | Set-Content -Encoding ASCII $coreOptions
$env:ARCDROID_TEST_POWER_ON_SHIFT = "1"
$powerOnRun = $null
try {
    $powerContent = (Resolve-Path (Join-Path $InputDirectory "arcdroid-test.adf")).Path
    $powerOnRun = Start-MediaRun $powerContent "slice18-power-on"
    foreach ($count in 1, 2) {
        if ($count -eq 2) { Send-MediaCommand $powerOnRun "RESET" }
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            $powerLines = @(Get-Content $powerOnRun.RetroLog -ErrorAction SilentlyContinue)
            $released = @($powerLines | Where-Object { $_ -match 'Power-on: shift released at=\d+ reason=timer' })
            $acknowledged = @($powerLines | Where-Object {
                $_.Contains('KART guest->device value=33 state=3')
            })
            if ($released.Count -ge $count -and $acknowledged.Count -ge (2 * $count)) {
                break
            }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($released.Count -lt $count -or $acknowledged.Count -lt (2 * $count)) {
            throw "Slice 18 boot $count did not release Shift and acknowledge both KART pairs; log=[$(Get-RunLogTail $powerOnRun)]"
        }
    }
    $powerCapture = Capture-RawScreenshot $powerOnRun "slice18-power-on-boot"
    if ($powerCapture.Colours -lt 2) { throw "Slice 18 authored guest did not draw its boot result" }
} finally {
    if ($powerOnRun) { Stop-MediaRun $powerOnRun }
    Remove-Item Env:ARCDROID_TEST_POWER_ON_SHIFT -ErrorAction SilentlyContinue
}
$powerLines = @(Get-Content $powerOnRun.RetroLog)
foreach ($pattern in @(
    'Power-on: boot-flag=clear shift=held',
    'Power-on: shift released at=',
    'KART device->guest values=C4,CC',
    'KART device->guest values=D4,DC',
    'FDC command=80 type=8')) {
    if (@($powerLines | Where-Object { $_.Contains($pattern) }).Count -lt 2) {
        throw "Slice 18 expected two guest-visible $pattern events across RESET"
    }
}
Write-Host "Slice 18 Windows RetroArch: synthetic power-on Shift down/up, floppy boot read and frontend RESET reconstruction passed"

# A test-core harness writes an invented completed product through the normal
# hard-disc store. Each RetroArch run below is a fresh process with no RESET.
$seedRoot = Join-Path $WorkDirectory ("slice23-product-seed-" + [Guid]::NewGuid().ToString('N'))
$seedTool = Join-Path $InputDirectory 'windows_load_harness.exe'
$testCore = Join-Path $InputDirectory 'arcdroid_libretro_test.dll'
$testRom = Join-Path $InputDirectory 'arcdroid-test.rom'
$seedOutput = @(& $seedTool $testCore $testRom $fContent $seedRoot '--seed-product' 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "Slice 23 product fixture seeding failed: $($seedOutput -join ' | ')"
}
$seedLines = @($seedOutput | Where-Object { $_ -match '^Seeded product: id=([0-9a-f]{40})$' })
if ($seedLines.Count -ne 1) { throw 'Slice 23 product seeder did not report one content id' }
$seedId = [regex]::Match([string]$seedLines[0], 'id=([0-9a-f]{40})').Groups[1].Value
if ($seedId -ne $fHardDiscId) { throw 'Slice 23 fixture content id changed between frontends' }
$seedStore = Join-Path $seedRoot "save/arcdroid/hd/$seedId"
if (-not (Test-Path (Join-Path $seedStore 'ready.a'))) {
    throw 'Slice 23 fixture store was not committed'
}
Remove-Item -LiteralPath $fHardDiscStore -Recurse -Force
Copy-Item -LiteralPath $seedStore -Destination $fHardDiscStore -Recurse -Force
if (-not (Test-Path (Join-Path $fHardDiscStore 'ready.a'))) {
    throw 'Slice 23 prepared store was not copied to the RetroArch save directory'
}
Copy-Item -Force $testCore $core
Copy-Item -Force $testRom $firmware
@"
arcdroid_machine = "Auto"
arcdroid_cpu = "Auto"
arcdroid_launch = "auto"
arcdroid_launch_chooser = "remember"
arcdroid_disk_writes = "shadow"
arcdroid_hard_disc = "auto"
"@ | Set-Content -Encoding ASCII $coreOptions
$productHelper = Join-Path $saveDirectory "ArcDroid/arcdroid/launch/$seedId.adl"
for ($attempt = 1; $attempt -le 2; $attempt++) {
    $productRun = Start-MediaRun $fContent "slice23-hd-product-$attempt"
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            $productLines = @(Get-Content $productRun.RetroLog -ErrorAction SilentlyContinue)
            if (@($productLines | Where-Object { $_.Contains('Hard disc scope: state=completed') }).Count -and
                @($productLines | Where-Object { $_.Contains('reason=hd-product') }).Count) { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if (-not @($productLines | Where-Object { $_.Contains('Hard disc scope: state=completed') }).Count -or
            -not @($productLines | Where-Object { $_.Contains('reason=hd-product') }).Count) {
            throw "Slice 23 process $attempt did not select the installed product: [$($productLines -join ' | ')]"
        }
        $capture = Capture-RawScreenshot $productRun "slice23-hd-product-$attempt"
        if ($capture.Colours -lt 2) { throw "Slice 23 process $attempt did not present a guest frame" }
        if (-not (Test-Path $productHelper)) { throw 'Slice 23 installed-product helper is missing' }
        $helperText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($productHelper))
        if (-not $helperText.Contains('%Filer_Run ADFS::4.$.ArcDroid.!Result')) {
            throw 'Slice 23 helper did not dispatch the fixture product from the hard disc'
        }
    } finally { Stop-MediaRun $productRun }
}
Write-Host 'Slice 23 Windows RetroArch: fixture hd-product helper and fresh-process reload passed'

# Authored ROM: a user SWI enters a ROM SVC error-box loop. RetroArch's
# keyboard-to-RetroPad binding gives the native frontend a deterministic A.
$errorContent = Join-Path $WorkDirectory 'error-box-content.adf'
Copy-Item -Force (Join-Path $InputDirectory 'arcdroid-test.adf') $errorContent
Copy-Item -Force (Join-Path $InputDirectory 'arcdroid-error-box.rom') $firmware
Copy-Item -Force (Join-Path $InputDirectory 'arcdroid_libretro_test.dll') $core
Add-Content -Encoding ASCII $config 'input_player1_a = "x"'
for ($layout = 0; $layout -lt 2; $layout++) {
    $mapping = if ($layout -eq 0) { 'Auto' } else { 'return' }
@"
arcdroid_machine = "A3000"
arcdroid_launch = "off"
arcdroid_pad1_a = "$mapping"
"@ | Set-Content -Encoding ASCII $coreOptions
    $errorRun = Start-MediaRun $errorContent "slice25-error-box-$layout"
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            $errorLines = @(Get-Content $errorRun.RetroLog -ErrorAction SilentlyContinue)
            if (@($errorLines | Where-Object { $_.Contains('Error box: ready g=') }).Count -and
                @($errorLines | Where-Object { $_.Contains('Error box: A = OK') }).Count) { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if (-not @($errorLines | Where-Object { $_.Contains('Error box: ready g=') }).Count -or
            -not @($errorLines | Where-Object { $_.Contains('Error box: A = OK') }).Count) {
            throw "Slice 25 layout $layout did not reach the authored error box: [$($errorLines -join ' | ')]"
        }
        if ($layout -eq 1 -and -not @($errorLines | Where-Object {
                $_.Contains('p1.A=Return[option]') }).Count) {
            throw 'Slice 25 custom Pad Mapping was not active'
        }
        [void](Set-ChooserWindowForeground $errorRun)
        Send-HardwareScanCode 0x2d $false "error-box-A-$layout" 150
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            $errorLines = @(Get-Content $errorRun.RetroLog -ErrorAction SilentlyContinue)
            if (@($errorLines | Where-Object { $_.Contains('Error box: answer g=1 result=ok key=27 state=consumed') }).Count -and
                @($errorLines | Where-Object { $_.Contains('Error box: ended g=1') }).Count) { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if (-not @($errorLines | Where-Object { $_.Contains('Error box: answer g=1 result=ok key=27 state=consumed') }).Count -or
            -not @($errorLines | Where-Object { $_.Contains('Error box: ended g=1') }).Count) {
            throw "Slice 25 layout $layout did not consume A at the authored poll: [$($errorLines -join ' | ')]"
        }
        if ($errorRun.Process.HasExited) { throw "Slice 25 layout $layout exited after dismissal" }
        $errorCapture = Capture-RawScreenshot $errorRun "slice25-error-box-$layout"
        if ($errorCapture.Dimensions -ne '640x512') {
            throw "Slice 25 layout $layout presented the wrong guest dimensions after dismissal: $($errorCapture.Dimensions)"
        }
        Start-Sleep -Milliseconds 200
        Send-HardwareScanCode 0x2d $false "error-box-restored-A-$layout" 150
        $expectedKey = if ($layout -eq 0) { 32 } else { 13 }
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            $errorLines = @(Get-Content $errorRun.RetroLog -ErrorAction SilentlyContinue)
            if (@($errorLines | Where-Object {
                    $_.Contains("Error box test: mapping restored key=$expectedKey") }).Count) { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if (-not @($errorLines | Where-Object {
                $_.Contains("Error box test: mapping restored key=$expectedKey") }).Count) {
            throw "Slice 25 layout $layout did not restore its Pad Mapping after release"
        }
    } finally { Stop-MediaRun $errorRun }
}
Write-Host 'Slice 25 Windows RetroArch: authored error box, hint, default and custom Pad Mapping passed'

# A ROM-owned FileCore request selects disc 2. Frontend Previous then moves
# from the core-reported index 1 to index 0. A fresh process starts on disc 1.
Copy-Item -Force (Join-Path $InputDirectory 'arcdroid-disc-request-a.rom') $firmware
Copy-Item -Force (Join-Path $InputDirectory 'arcdroid_libretro_test.dll') $core
$requestSet = Join-Path $InputDirectory 'disc-request/Set.m3u'
@"
arcdroid_machine = "A3000"
arcdroid_launch = "off"
arcdroid_disc_requests = "auto"
"@ | Set-Content -Encoding ASCII $coreOptions
function Wait-DiscInsertion($Run, [int]$Count) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $lines = @(Get-Content $Run.RetroLog -ErrorAction SilentlyContinue |
            Where-Object { $_.Contains('Disc request: signal=A name=Two candidate=2 action=inserted') })
        if ($lines.Count -ge $Count) { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "$($Run.Name) expected $Count automatic disc-2 insertions; log=[$(Get-RunLogTail $Run)]"
}
function Wait-DiscLogLine($Run, [string]$Text) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if (@(Get-Content $Run.RetroLog -ErrorAction SilentlyContinue |
            Where-Object { $_.Contains($Text) }).Count) { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "$($Run.Name) expected '$Text'; log=[$(Get-RunLogTail $Run)]"
}
$requestRun = Start-MediaRun $requestSet 'slice26-disc-request'
try {
    Wait-DiscInsertion $requestRun 1
    Send-MediaCommand $requestRun 'DISK_EJECT_TOGGLE'
    Wait-DiscLogLine $requestRun 'Ejected virtual disc tray.'
    Send-MediaCommand $requestRun 'DISK_PREV'
    Wait-DiscLogLine $requestRun 'Setting disc in tray: 1/2 - One'
    Send-MediaCommand $requestRun 'DISK_EJECT_TOGGLE'
    Wait-DiscLogLine $requestRun 'Closed virtual disc tray.'
} finally { Stop-MediaRun $requestRun }
$requestReload = Start-MediaRun $requestSet 'slice26-disc-request-reload'
try {
    Wait-DiscLogLine $requestReload 'Launch: boot=0 disc="One"'
    Wait-DiscInsertion $requestReload 1
}
finally { Stop-MediaRun $requestReload }
Write-Host 'Slice 26 Windows RetroArch: automatic insertion, Disk Control previous-index selection and fresh-process launch image passed'

# R2c AC-9: one authored RPC ROM; disc markers alone choose the three sessions.
# Keep this after the existing Arculator legs so their core/config/save paths
# and behaviour remain unchanged. The guarded-JFD-only run needs no RPC leg.
if (-not $GuardedJfdOnly) {
    $rpcStarted = [DateTime]::UtcNow
    $rpcFixture = (Resolve-Path (Join-Path $InputDirectory 'rpc-hd-retroarch')).Path
    $rpcSave = Join-Path $WorkDirectory 'rpc-hd-save'
    if (Test-Path $rpcSave) { throw 'R2c evidence requires a fresh rpc-hd-save directory' }
    New-Item -ItemType Directory $rpcSave | Out-Null
    Copy-Item -Recurse (Join-Path $rpcFixture 'save/arcdroid') $rpcSave
    $rpcSave = (Resolve-Path $rpcSave).Path
    $rpcCore = Join-Path $coreDirectory 'arcdroid_rpcplan_libretro.dll'
    Copy-Item (Join-Path $InputDirectory 'arcdroid_rpcplan_libretro.dll') $rpcCore
    Copy-Item (Join-Path $InputDirectory 'arcdroid_rpcplan_libretro.info') $infoDirectory
    $rpcConfig = Join-Path $WorkDirectory 'rpc-hd.cfg'
    # RetroArch keeps the first occurrence of a config key. Replace the shared
    # leg's values instead of appending duplicate directory keys.
    Get-Content -LiteralPath $config | Where-Object {
        $_ -notmatch '^\s*(system_directory|savefile_directory|savestate_directory|sort_savefiles_enable|sort_savefiles_by_content_enable)\s*='
    } | Set-Content -Encoding ASCII $rpcConfig
@"
system_directory = "$(ConfigPath $rpcFixture)"
savefile_directory = "$(ConfigPath $rpcSave)"
savestate_directory = "$(ConfigPath $rpcSave)"
sort_savefiles_enable = "false"
sort_savefiles_by_content_enable = "false"
video_shader_enable = "false"
screenshot_gpu = "false"
screenshot_directory = "$(ConfigPath ((Resolve-Path $evidenceDirectory).Path))"
"@ | Add-Content -Encoding ASCII $rpcConfig
    # Start-Process joins ArgumentList with spaces; quote all new leg paths.
    $config = '"' + (Resolve-Path $rpcConfig).Path + '"'
    $core = '"' + (Resolve-Path $rpcCore).Path + '"'
    $rpcPlan = '"' + (Join-Path $rpcFixture 'hd.arcplan') + '"'
    $rpcRoot = Join-Path $rpcSave ('arcdroid/hd-riscpc/' + ('c' * 40))
    $rpcDumpDirectory = Join-Path $evidenceDirectory 'rpc-hd-crash'
    New-Item -ItemType Directory -Force $rpcDumpDirectory | Out-Null
    $rpcDumpDirectory = (Resolve-Path $rpcDumpDirectory).Path
    Copy-Item -LiteralPath $rpcCore -Destination $rpcDumpDirectory
    foreach ($file in @('rpc-hd-symbols.txt', 'rpc-hd-image.txt', 'rpc-hd-revision.txt')) {
        Copy-Item -LiteralPath (Join-Path $InputDirectory $file) -Destination $rpcDumpDirectory
    }
    "core_sha256=$((Get-FileHash -Algorithm SHA256 -LiteralPath $rpcCore).Hash)" |
        Set-Content -Encoding ASCII (Join-Path $rpcDumpDirectory 'core-sha256.txt')
@'
The DLL is the exact Release (-O3 -DNDEBUG) image loaded by these sessions,
with -g DWARF retained. cdb provides the runtime module base and fault RIP;
it need not understand MinGW DWARF. Compute RVA = RIP - runtime module base,
then address = preferred ImageBase (rpc-hd-image.txt) + RVA. With matching
MinGW binutils, map to source and inlined callers using:
  addr2line -e arcdroid_rpcplan_libretro.dll -f -C -i <address>
  objdump -d -l arcdroid_rpcplan_libretro.dll
rpc-hd-symbols.txt is sorted by preferred virtual address. The commit and
DLL SHA-256 accompany it. Never use symbols from a separately rebuilt DLL.
'@ | Set-Content -Encoding ASCII (Join-Path $rpcDumpDirectory 'source-mapping.txt')
    function Enable-RpcHdCrashDumps {
        $parent = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'
        $key = "$parent\retroarch.exe"
        $settings = $null
        try {
            $parentExisted = Test-Path -LiteralPath $parent
            $existed = Test-Path -LiteralPath $key
            $previous = @{}
            if ($existed) {
                $item = Get-Item -LiteralPath $key
                foreach ($name in @('DumpFolder', 'DumpType', 'DumpCount')) {
                    if ($item.GetValueNames() -contains $name) {
                        $previous[$name] = @{ Value = $item.GetValue($name, $null,
                            [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames);
                            Kind = $item.GetValueKind($name) }
                    }
                }
            }
            $settings = @{ Key = $key; Existed = $existed; Previous = $previous;
                Parent = $parent; ParentExisted = $parentExisted }
            # New-Item -Force can replace an existing registry key. Preserve
            # every pre-existing property, including settings we do not own.
            if (-not $existed) { New-Item -Path $key -Force | Out-Null }
            New-ItemProperty -LiteralPath $key -Name DumpFolder -PropertyType ExpandString -Value $rpcDumpDirectory -Force | Out-Null
            New-ItemProperty -LiteralPath $key -Name DumpType -PropertyType DWord -Value 2 -Force | Out-Null
            New-ItemProperty -LiteralPath $key -Name DumpCount -PropertyType DWord -Value 3 -Force | Out-Null
        } catch {
            if ($settings) {
                try { Restore-RpcHdCrashDumps $settings }
                catch { Write-Warning "RPC WER settings restore failed: $($_.Exception.Message)" }
            }
            Write-Warning "RPC WER LocalDumps unavailable (HKLM may not be writable): $($_.Exception.Message). Continuing with the first-chance locator."
            return $null
        }
        Write-Host "RPC-only WER full dumps enabled: $rpcDumpDirectory"
        return $settings
    }
    function Restore-RpcHdCrashDumps($Settings) {
        if (-not $Settings) { return }
        if (-not $Settings.Existed) {
            if (Test-Path -LiteralPath $Settings.Key) { Remove-Item -LiteralPath $Settings.Key }
            if (-not $Settings.ParentExisted -and (Test-Path -LiteralPath $Settings.Parent)) {
                $item = Get-Item -LiteralPath $Settings.Parent
                if ($item.SubKeyCount -eq 0 -and $item.ValueCount -eq 0) {
                    Remove-Item -LiteralPath $Settings.Parent
                }
            }
            return
        }
        foreach ($name in @('DumpFolder', 'DumpType', 'DumpCount')) {
            if ($Settings.Previous.ContainsKey($name)) {
                $old = $Settings.Previous[$name]
                New-ItemProperty -LiteralPath $Settings.Key -Name $name -PropertyType $old.Kind -Value $old.Value -Force | Out-Null
            } else {
                Remove-ItemProperty -LiteralPath $Settings.Key -Name $name -ErrorAction SilentlyContinue
            }
        }
    }
    function Save-RpcHdExceptionEvidence($Run) {
        try {
            $locator = Join-Path $rpcSave 'rpc-hd-exception.txt'
            if (Test-Path -LiteralPath $locator) {
                Copy-Item -Force -LiteralPath $locator -Destination (Join-Path $rpcDumpDirectory "$($Run.Name)-exception.txt")
            }
        } catch { Write-Warning "RPC exception evidence copy failed: $($_.Exception.Message)" }
    }
    function Save-RpcHdCrashEvidence($Run) {
        Save-RpcHdExceptionEvidence $Run
        if ($Run['RpcHdForceStopped'] -or -not $Run.Process.HasExited -or $Run.Process.ExitCode -eq 0) { return }
        $summary = Join-Path $rpcDumpDirectory "$($Run.Name)-capture.txt"
        try {
            "pid=$($Run.Process.Id) exit_code=$($Run.Process.ExitCode)" | Set-Content -Encoding ASCII $summary
            $deadline = [DateTime]::UtcNow.AddSeconds(30)
            do {
                $dumps = @(Get-ChildItem -LiteralPath $rpcDumpDirectory -Filter 'retroarch*.dmp' |
                    Where-Object { $_.Name -match ('\.' + $Run.Process.Id + '\.') -and
                        $_.LastWriteTimeUtc -ge $Run.StartUtc -and $_.Length -gt 0 })
                if ($dumps.Count) { break }
                Start-Sleep -Milliseconds 250
            } while ([DateTime]::UtcNow -lt $deadline)
            if (-not $dumps.Count) {
                'No WER dump appeared for this PID within 30 s; logs and matching DLL/symbols retained.' |
                    Add-Content -Encoding ASCII $summary
                Write-Warning "No WER dump for $($Run.Name); see $summary"
                return
            }
            $cdb = Get-Command cdb.exe -ErrorAction SilentlyContinue
            $debugger = if ($cdb) { $cdb.Source } else { $null }
            if (-not $debugger) {
                foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
                    if (-not $base) { continue }
                    $candidate = Join-Path $base 'Windows Kits\10\Debuggers\x64\cdb.exe'
                    if (Test-Path -LiteralPath $candidate) { $debugger = $candidate; break }
                }
            }
            if (-not $debugger) {
                'cdb absent; full dumps and matching DLL/symbols retained for offline analysis.' |
                    Add-Content -Encoding ASCII $summary
                Write-Warning "cdb is unavailable; dumps retained in $rpcDumpDirectory"
                return
            }
            foreach ($dump in $dumps) {
                $out = Join-Path $rpcDumpDirectory "$($Run.Name)-cdb.txt"
                $err = Join-Path $rpcDumpDirectory "$($Run.Name)-cdb.stderr.txt"
                $commands = '.exr -1; .ecxr; r; kv 30; lmv m arcdroid*; !teb; ~* kv 8; q'
                "dump=$($dump.Name) cdb=$debugger commands=$commands" | Add-Content -Encoding ASCII $summary
                $arguments = @('-z', ('"' + $dump.FullName + '"'), '-i', ('"' + $rpcDumpDirectory + '"'),
                    '-y', ('"' + $rpcDumpDirectory + '"'), '-c', ('"' + $commands + '"'))
                $process = Start-Process -FilePath $debugger -ArgumentList $arguments -PassThru -NoNewWindow `
                    -RedirectStandardOutput $out -RedirectStandardError $err
                if (-not $process.WaitForExit(60000)) {
                    Stop-Process -Id $process.Id -Force
                    $process.WaitForExit()
                    'cdb timed out after 60 s; partial output and dump retained.' | Add-Content -Encoding ASCII $summary
                } else {
                    $process.WaitForExit()
                    "cdb_exit_code=$($process.ExitCode)" | Add-Content -Encoding ASCII $summary
                }
            }
        } catch {
            Write-Warning "RPC dump analysis failed without changing the original failure: $($_.Exception.Message)"
        }
    }
    Add-Type -TypeDefinition @'
using System;
public static class ArcRpcHdCrc {
    public static uint Calculate(byte[] b, int start, int count) {
        uint c = 0xffffffff;
        for (int i = start; i < start + count; ++i) {
            c ^= b[i];
            for (int bit = 0; bit < 8; ++bit)
                c = (c >> 1) ^ ((c & 1) != 0 ? 0xedb88320u : 0u);
        }
        return c ^ 0xffffffff;
    }
}
'@
    function Test-RpcHdStore([string]$Name, [UInt64]$Minimum) {
        Get-ChildItem -LiteralPath $rpcRoot | Sort-Object Name |
            Select-Object Name, Length | Format-Table -AutoSize | Out-String |
            Set-Content -Encoding UTF8 (Join-Path $evidenceDirectory "$Name-store.txt")
        $manifests = @(Get-ChildItem -LiteralPath $rpcRoot -Filter 'manifest.?')
        $journals = @(Get-ChildItem -LiteralPath $rpcRoot -Filter 'journal.?')
        if ($manifests.Count -ne 1 -or $journals.Count -ne 1) {
            throw "$Name checkpoint did not finish RETIRE/GC"
        }
        $m = [IO.File]::ReadAllBytes($manifests[0].FullName)
        if ($m.Length -ne 24324 -or [Text.Encoding]::ASCII.GetString($m, 0, 8) -ne 'ARCRHDM1' -or
            [Text.Encoding]::ASCII.GetString($m, 16, 40) -ne ('c' * 40) -or
            [ArcRpcHdCrc]::Calculate($m, 0, 24320) -ne [BitConverter]::ToUInt32($m, 24320)) {
            throw "$Name manifest framing, identity or CRC failed"
        }
        $generation = [BitConverter]::ToUInt64($m, 88)
        $expectedManifest = if (($generation % 2) -eq 0) { 'manifest.a' } else { 'manifest.b' }
        if ($manifests[0].Name -ne $expectedManifest) { throw "$Name noncanonical manifest slot" }
        if ($generation -lt $Minimum) { throw "$Name expected manifest generation >= $Minimum; got $generation" }
        $geometry = @(512, 63, 16, 256, 262144, 504)
        for ($i = 0; $i -lt 6; $i++) {
            if ([BitConverter]::ToUInt32($m, 64 + 4 * $i) -ne $geometry[$i]) { throw "$Name geometry failed" }
        }
        $known = [System.Collections.Generic.HashSet[string]]::new()
        [void]$known.Add($manifests[0].Name); [void]$known.Add($journals[0].Name)
        for ($i = 0; $i -lt 504; $i++) {
            $entry = 128 + 48 * $i
            $kind = [BitConverter]::ToUInt32($m, $entry)
            if ($kind -eq 0) { continue }
            if ($kind -ne 1) { throw "$Name invalid chunk kind" }
            $chunkGen = [BitConverter]::ToUInt64($m, $entry + 8)
            $leaf = 'c{0:x3}.g{1:x16}' -f $i, $chunkGen
            [void]$known.Add($leaf)
            $b = [IO.File]::ReadAllBytes((Join-Path $rpcRoot $leaf))
            if ($b.Length -ne 262276 -or [Text.Encoding]::ASCII.GetString($b, 0, 8) -ne 'ARCRHDC1' -or
                [ArcRpcHdCrc]::Calculate($b, 0, 124) -ne [BitConverter]::ToUInt32($b, 124) -or
                [ArcRpcHdCrc]::Calculate($b, 128, 262144) -ne [BitConverter]::ToUInt32($b, 262272)) {
                throw "$Name chunk $leaf framing/CRC failed"
            }
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $digest = $sha.ComputeHash($b, 128, 262144) } finally { $sha.Dispose() }
            for ($k = 0; $k -lt 32; $k++) {
                if ($digest[$k] -ne $m[$entry + 16 + $k] -or $digest[$k] -ne $b[80 + $k]) {
                    throw "$Name chunk $leaf digest failed"
                }
            }
        }
        foreach ($file in @(Get-ChildItem -LiteralPath $rpcRoot)) {
            if (-not $known.Contains($file.Name)) { throw "$Name leftover/unknown canonical file $($file.Name)" }
        }
        $j = [IO.File]::ReadAllBytes($journals[0].FullName)
        if ($j.Length -lt 256 -or $j.Length -gt 8388608 -or
            [Text.Encoding]::ASCII.GetString($j, 0, 8) -ne 'ARCRHDJ1' -or
            [ArcRpcHdCrc]::Calculate($j, 0, 252) -ne [BitConverter]::ToUInt32($j, 252) -or
            [BitConverter]::ToUInt64($j, 88) -ne [BitConverter]::ToUInt64($m, 96)) {
            throw "$Name journal header/binding failed"
        }
        "$Name manifest_generation=$generation chunks=$($known.Count - 2) journal_bytes=$($j.Length) RETIRE/GC=complete" |
            Add-Content -Encoding ASCII (Join-Path $evidenceDirectory 'rpc-hd-results.txt')
        return $generation
    }
    function Wait-RpcHdDirectories($Run) {
        $expected = @{ SYSTEM = $rpcFixture; SAVE = $rpcSave }
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            $seen = @{}
            if (Test-Path -LiteralPath $Run.RetroLog) {
                foreach ($line in @(Get-Content -LiteralPath $Run.RetroLog)) {
                    if ($line -match 'GET_(SYSTEM|SAVE)_DIRECTORY:\s*"([^"]*)"') {
                        $kind = $Matches[1]
                        $actual = $Matches[2].Replace('\', '/').TrimEnd('/')
                        $wanted = $expected[$kind].Replace('\', '/').TrimEnd('/')
                        if (-not [string]::Equals($actual, $wanted, [StringComparison]::OrdinalIgnoreCase)) {
                            throw "$($Run.Name) GET_${kind}_DIRECTORY expected '$wanted'; got '$actual'"
                        }
                        $seen[$kind] = $true
                    }
                }
            }
            if ($seen.SYSTEM -and $seen.SAVE) {
                Write-Host "$($Run.Name) frontend directories verified: system=$rpcFixture save=$rpcSave"
                return
            }
            if ($Run.Process.HasExited) { throw "$($Run.Name) exited before both frontend directories were verified" }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        throw "$($Run.Name) missing GET_SYSTEM_DIRECTORY or GET_SAVE_DIRECTORY in its RetroArch log"
    }
    function Wait-RpcHdScreen($Run, [int]$Rgb) {
        Wait-RpcHdDirectories $Run
        $deadline = [DateTime]::UtcNow.AddSeconds(90)
        $attempt = 0
        do {
            $attempt++
            $shot = Capture-RawScreenshot $Run "$($Run.Name)-$attempt"
            $image = [System.Drawing.Bitmap]::new($shot.Path)
            try {
                $match = $true
                foreach ($x in @(0.25, 0.5, 0.75)) {
                    foreach ($y in @(0.25, 0.5, 0.75)) {
                        $actual = $image.GetPixel([int]($image.Width*$x), [int]($image.Height*$y)).ToArgb() -band 0xffffff
                        if ($actual -eq 0xff0000) { throw "$($Run.Name) authored guest FAIL (red)" }
                        if ($actual -ne $Rgb) { $match = $false }
                    }
                }
                if ($match) { return }
            } finally { $image.Dispose() }
            Start-Sleep -Milliseconds 750
        } while ([DateTime]::UtcNow -lt $deadline)
        throw "$($Run.Name) did not reach expected RGB $('{0:x6}' -f $Rgb) within 90 seconds"
    }
    function Save-RpcHdLogs($Run) {
        if (-not (Test-Path -LiteralPath $Run.RetroLog)) {
            throw "$($Run.Name) did not produce its RetroArch log"
        }
        @($Run.Stdout, $Run.Stderr) | ForEach-Object {
            if (Test-Path -LiteralPath $_) { Copy-Item -Force $_ $evidenceDirectory }
        }
        $lines = @($Run.RetroLog, $Run.Stdout, $Run.Stderr, (Join-Path $evidenceDirectory "$($Run.Name)-process.log")) | ForEach-Object {
            if (Test-Path -LiteralPath $_) { Get-Content -LiteralPath $_ }
        }
        if (@($lines | Where-Object { $_ -match '(?i)RiscPC HD phase=|saved RiscPC hard disc is damaged|Hard-disc changes can no longer be saved|RPC plan refused|RPC default generation|Timed RPC disk-control operation refused|\[ERROR\]' }).Count) {
            throw "$($Run.Name) damage/refusal/frontend error; see collected logs"
        }
    }
    $rpcDumpSettings = Enable-RpcHdCrashDumps
    try {
    $previous = [UInt64]0
    foreach ($session in @(1, 2, 3)) {
        $run = $null
        try {
            $run = Start-MediaRun $rpcPlan "rpc-hd-session-$session"
            $expected = @(0x00ffff, 0x0000ff, 0x00ff00)[$session - 1]
            Wait-RpcHdScreen $run $expected
            if ($session -eq 1) {
                # No unload: the screenshot proves marker acknowledgement.
                $run['RpcHdForceStopped'] = $true  # intentional termination is not a WER crash
                Stop-Process -Id $run.Process.Id -Force
                if (-not $run.Process.WaitForExit(30000)) { throw 'R2c force termination timed out' }
                $run.Udp.Dispose()
            } else {
                Stop-MediaRun $run  # existing network QUIT pair, exit code 0
            }
            Save-RpcHdLogs $run
            $minimum = if ($session -eq 3) { $previous } else { $previous + 1 }
            $observed = Test-RpcHdStore $run.Name $minimum
            if ($session -eq 3 -and $observed -ne $previous) { throw 'Verify-only session changed HD generation' }
            $previous = $observed
        } catch {
            if ($run) { Write-MediaRunFailureEvidence $run 'R2c authored hard-disc evidence failed' }
            throw
        } finally {
            if ($run) {
                if (-not $run.Process.HasExited) { Stop-Process -Id $run.Process.Id -Force; $run.Process.WaitForExit() }
                Save-RpcHdExceptionEvidence $run
                $run.Udp.Dispose()
                @($run.Stdout, $run.Stderr) | ForEach-Object {
                    if (Test-Path -LiteralPath $_) { Copy-Item -Force $_ $evidenceDirectory }
                }
            }
            if (Test-Path -LiteralPath $rpcRoot) {
                Get-ChildItem -LiteralPath $rpcRoot | Sort-Object Name | Select-Object Name, Length |
                    Format-Table -AutoSize | Out-String |
                    Set-Content -Encoding UTF8 (Join-Path $evidenceDirectory "rpc-hd-session-$session-store.txt")
            }
        }
    }
    } finally { Restore-RpcHdCrashDumps $rpcDumpSettings }
    $rpcElapsed = ([DateTime]::UtcNow - $rpcStarted).TotalSeconds
    $result = "R2c native Windows RetroArch: force termination, reload gen1, write gen2, QUIT, reload gen2, timed plan disk-control callbacks and full IDENTIFY/data comparisons PASS; added seconds=$('{0:F1}' -f $rpcElapsed)"
    Write-Host $result
    $result | Add-Content -Encoding ASCII (Join-Path $evidenceDirectory 'rpc-hd-results.txt')
    if ($env:GITHUB_STEP_SUMMARY) { Add-Content -Encoding UTF8 $env:GITHUB_STEP_SUMMARY "- $result" }
    . (Join-Path $PSScriptRoot 'test-retroarch-rpc-privacy.ps1')
}

if ($OrdinaryRouteBuild) { . (Join-Path $PSScriptRoot 'test-retroarch-rpc-route.ps1') }
