#Requires -RunAsAdministrator
# Unprotects the 533c's sealed PSK, saved by extract-windows-psk.sh --533c, as
# SYSTEM, the account Dell's driver sealed it with, and saves the key as
# hexadecimal for install-windows-psk.sh --533c.
param(
    [Parameter(Mandatory = $true)][string]$BlobFile,
    [Parameter(Mandatory = $true)][string]$OutputFile
)

$ErrorActionPreference = 'Stop'
$blob = (Resolve-Path -LiteralPath $BlobFile).ProviderPath
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputFile)
if (Test-Path -LiteralPath $output) { throw "Output already exists: $output" }

$name = 'GoodixPsk-' + [guid]::NewGuid().ToString('N')
$work = Join-Path $env:ProgramData $name
# Created with its ACL: full access for SYSTEM and Administrators, and nothing
# for anyone else.
$acl = New-Object Security.AccessControl.DirectorySecurity
$acl.SetSecurityDescriptorSddlForm('D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)')
[IO.Directory]::CreateDirectory($work, $acl) | Out-Null
try {
    Copy-Item -LiteralPath $blob -Destination (Join-Path $work 'blob.bin')
    $worker = Join-Path $work 'unprotect.ps1'
    Set-Content -LiteralPath $worker -Encoding ASCII -Value @'
$ErrorActionPreference = 'Stop'
try {
    Add-Type -AssemblyName System.Security
    $blob = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'blob.bin'))
    $key = [Security.Cryptography.ProtectedData]::Unprotect(
        $blob, $null, [Security.Cryptography.DataProtectionScope]::CurrentUser)
    if ($key.Length -ne 32) { exit 1 }
    $hex = [BitConverter]::ToString($key).Replace('-', '').ToLowerInvariant()
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'key.hex'), "$hex`n")
    exit 0
} catch { exit 1 }
'@

    $action = New-ScheduledTaskAction -Execute "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
        -Argument "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File `"$worker`""
    # S-1-5-18 is SYSTEM, whatever the system language.
    $principal = New-ScheduledTaskPrincipal -UserId 'S-1-5-18' -LogonType ServiceAccount
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    Register-ScheduledTask -TaskName $name -Action $action -Principal $principal -Settings $settings | Out-Null
    $notRun = (Get-ScheduledTaskInfo -TaskName $name).LastRunTime
    Start-ScheduledTask -TaskName $name
    $deadline = (Get-Date).AddMinutes(1)
    do {
        if ((Get-Date) -gt $deadline) { throw 'Unprotecting the key timed out.' }
        Start-Sleep -Milliseconds 200
        $info = Get-ScheduledTaskInfo -TaskName $name
        $state = (Get-ScheduledTask -TaskName $name).State
    } while ($info.LastRunTime -eq $notRun -or $state -eq 'Running' -or $state -eq 'Queued')
    $info = Get-ScheduledTaskInfo -TaskName $name
    if ($info.LastTaskResult -ne 0) {
        throw 'Could not unprotect the key. Run this in the Windows installation that last used the sensor.'
    }
    [IO.File]::Copy((Join-Path $work 'key.hex'), $output)
} finally {
    Stop-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $name -Confirm:$false -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $work -Recurse -Force
}
Write-Host "Saved the Windows key to $output"
