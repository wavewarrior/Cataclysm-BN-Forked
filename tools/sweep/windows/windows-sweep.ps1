<#
.SYNOPSIS
  One nightly Windows platform sweep: fast-forward to origin, then `deno task sweep`.

.DESCRIPTION
  Skips (exit 0) when a build or test process is already running, so it never competes with an
  agent lane for the machine. Refuses to touch a working tree with local changes. The sweep itself
  decides what is new from `.sweep/windows.watermark`.
#>
param(
  [string] $RepoDir = "C:\WORK\GIT_REPOS\Cataclysm-BN-Forked",
  [string] $Branch = "feature/improvements"
)

$ErrorActionPreference = "Stop"
Set-Location $RepoDir

$busy = Get-Process -Name ninja, cl, link, cata_test-tiles -ErrorAction SilentlyContinue
if ($busy) {
  Write-Output "sweep skipped: $(($busy | Select-Object -ExpandProperty Name -Unique) -join ', ') is running"
  exit 0
}

$dirty = git status --porcelain --untracked-files=no
if ($dirty) {
  Write-Output "sweep skipped: the working tree has local changes"
  exit 0
}

git fetch --quiet origin $Branch
git checkout --quiet $Branch
git merge --ff-only "origin/$Branch"
if ($LASTEXITCODE -ne 0) {
  Write-Output "sweep skipped: $Branch cannot fast-forward to origin"
  exit 0
}

deno task sweep --platform windows
exit $LASTEXITCODE
