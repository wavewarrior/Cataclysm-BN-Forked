<#
.SYNOPSIS
  Registers (or previews) the nightly Windows platform sweep as a Task Scheduler task.

.DESCRIPTION
  The task runs `windows-sweep.ps1` next to this file at 03:00 every day as the logged-on user.
  A run that is missed (the machine was asleep) is simply skipped: the next one covers the gap,
  because the sweep works from the watermark file and not from the clock.

  Registering the task is the owner's step. Use -WhatIf first to see exactly what would be
  registered; nothing is created in that mode.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\sweep\windows\install-task.ps1 -WhatIf
  powershell -ExecutionPolicy Bypass -File tools\sweep\windows\install-task.ps1
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string] $RepoDir = "C:\WORK\GIT_REPOS\Cataclysm-BN-Forked",
  [string] $TaskName = "CBN platform sweep (windows)",
  [string] $At = "03:00"
)

$ErrorActionPreference = "Stop"

$script = Join-Path $PSScriptRoot "windows-sweep.ps1"
if (-not (Test-Path $script)) { throw "missing $script" }
if (-not (Test-Path $RepoDir)) { throw "repo not found: $RepoDir" }

$arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$script`" -RepoDir `"$RepoDir`""
$action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $arguments -WorkingDirectory $RepoDir
$trigger = New-ScheduledTaskTrigger -Daily -At $At
$settings = New-ScheduledTaskSettingsSet `
  -ExecutionTimeLimit (New-TimeSpan -Hours 2) `
  -StartWhenAvailable:$false `
  -MultipleInstances IgnoreNew `
  -AllowStartIfOnBatteries
$principal = New-ScheduledTaskPrincipal `
  -UserId ([System.Security.Principal.WindowsIdentity]::GetCurrent().Name) `
  -LogonType Interactive `
  -RunLevel Limited

if ($PSCmdlet.ShouldProcess($TaskName, "Register-ScheduledTask daily at $At running $script")) {
  Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
    -Settings $settings -Principal $principal -Force | Out-Null
  Write-Output "registered '$TaskName': daily at $At, 2 h limit, one instance, as $($principal.UserId)"
} else {
  Write-Output "would register '$TaskName': daily at $At"
  Write-Output "  run: powershell.exe $arguments"
  Write-Output "  limit: 2 h, one instance at a time, skipped (not caught up) when the machine was asleep"
}
