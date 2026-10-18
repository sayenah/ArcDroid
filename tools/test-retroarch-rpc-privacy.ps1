# R3d-3 F4. Dot-sourced after the existing RPC legs, using their process helpers.
# SPDX-License-Identifier: GPL-2.0-only
$privacyStarted = [DateTime]::UtcNow
foreach ($messagePath in @('extended', 'legacy')) {
    $contentSentinel = 'R3D3-CONTENT-SENTINEL'
    $saveSentinel = 'R3D3-SAVE-SENTINEL'
    $privacySave = Join-Path $WorkDirectory "$saveSentinel-$messagePath"
    if (Test-Path -LiteralPath $privacySave) { throw 'F4 requires fresh save directories' }
    New-Item -ItemType Directory $privacySave | Out-Null
    Copy-Item -Recurse (Join-Path $rpcFixture 'save/arcdroid') $privacySave
    $privacySave = (Resolve-Path $privacySave).Path
    $privacyContent = Join-Path $WorkDirectory "$contentSentinel-$messagePath.arcplan"
    $privacyMedia = Join-Path $WorkDirectory "$contentSentinel-$messagePath.adf"
    Copy-Item -LiteralPath (Join-Path $rpcFixture 'floppy.adf') $privacyMedia
    $privacyMedia = (Resolve-Path $privacyMedia).Path
    Get-Content -LiteralPath (Join-Path $rpcFixture 'hd.arcplan') | Where-Object {
        $_ -notmatch '^(media|disk_control)='
    } | Set-Content -Encoding ASCII $privacyContent
    @("media=$(ConfigPath $privacyMedia)", "message_interface=$messagePath") |
        Add-Content -Encoding ASCII $privacyContent
    $privacyConfig = Join-Path $WorkDirectory "rpc-privacy-$messagePath.cfg"
    # RetroArch takes the FIRST key occurrence. Replace instead of appending
    # duplicate keys. English fixes the source-derived frontend-owned forms.
    Get-Content -LiteralPath $rpcConfig | Where-Object {
        $_ -notmatch '^\s*(savefile_directory|savestate_directory|user_language|log_to_file_timestamp)\s*='
    } | Set-Content -Encoding ASCII $privacyConfig
    @"
savefile_directory = "$(ConfigPath $privacySave)"
savestate_directory = "$(ConfigPath $privacySave)"
user_language = "0"
log_to_file_timestamp = "false"
"@ | Add-Content -Encoding ASCII $privacyConfig
    $config = '"' + (Resolve-Path $privacyConfig).Path + '"'
    $run = $null
    try {
        # Start-MediaRun always passes --verbose and --log-file to pinned RA.
        $run = Start-MediaRun ('"' + (Resolve-Path $privacyContent).Path + '"') "rpc-privacy-$messagePath"
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        $marker = if ($messagePath -eq 'extended') { 'SET_MESSAGE_EXT' } else { 'SET_MESSAGE' }
        do {
            $captured = ''
            if (Test-Path -LiteralPath $run.RetroLog) {
                $captured = [string](Get-Content -Raw -LiteralPath $run.RetroLog)
            }
            if ($captured.Contains("RPC privacy probe: $marker accepted") -and
                $captured.Contains('N-7: Checking the RiscPC hard disc')) { break }
            if ($run.Process.HasExited) { throw 'F4 RetroArch exited before notice delivery' }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
    } finally {
        if ($run) { Stop-MediaRun $run }
    }
    # Read the complete, unmodified file AFTER QUIT, including unload notices.
    # tee preserves the classification of every sentinel line as CI evidence.
    & python -B (Join-Path $PSScriptRoot 'check-retroarch-rpc-privacy.py') $run.RetroLog `
        --sentinel $contentSentinel --sentinel $saveSentinel --message-path $messagePath |
        Tee-Object -FilePath (Join-Path $evidenceDirectory "rpc-privacy-$messagePath-classification.txt")
    if ($LASTEXITCODE -ne 0) { throw "F4 $messagePath catalogue privacy/liveness failed" }
}
$privacyElapsed = ([DateTime]::UtcNow - $privacyStarted).TotalSeconds
Write-Host "F4 RetroArch 1.22.2: both catalogue message paths PASS; added seconds=$('{0:F1}' -f $privacyElapsed)"
