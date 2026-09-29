# ---------------------------------------------------------------------------
# wave-list
#
# Every worktree, its branch, whether it has uncommitted changes, and how far
# it has drifted from the integration branch.
#
# Read-only. Safe to run at any time, including while other tasks are
# building.
#
# Usage:
#   scripts/harness/wave-list.ps1
# ---------------------------------------------------------------------------

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

. "$PSScriptRoot/lib/harness.ps1"

$repoRoot = Get-HarnessRepoRoot
Set-Location $repoRoot

$integration = Get-HarnessIntegrationBranch

# Fetch quietly so ahead/behind counts against the real remote. A missing
# remote is not an error: the harness works before the first push.
& git remote get-url origin > $null 2>&1
if ($LASTEXITCODE -eq 0) {
    & git fetch origin --quiet 2>$null
}

& git rev-parse --verify --quiet $integration > $null 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Error "integration branch '$integration' does not exist. Create it with: git branch $integration main"
    exit 1
}

# Column widths chosen from the longest realistic values rather than computed,
# so the output does not reflow between runs and is easy to read repeatedly.
$fmt = "{0,-28} {1,-28} {2,-10} {3,-9} {4}"
Write-Host ($fmt -f "WORKTREE", "BRANCH", "STATE", "AHEAD", "DIRTY")
Write-Host ($fmt -f "----------------------------", "----------------------------", "----------", "---------", "-----")

$path = $null
$branch = $null
$detached = $false

function Write-HarnessRow {
    param([string] $Path, [string] $Branch, [bool] $Detached, [string] $Integration)

    $name = Split-Path -Leaf $Path

    if (-not (Test-Path $Path)) {
        Write-Host ($fmt -f $name, $Branch, "missing", "-", "-")
        return
    }

    $state = if ($Detached) { "detached" } else { "ok" }
    $ahead = "-"

    if (-not $Detached -and $Branch) {
        & git rev-parse --verify --quiet $Branch > $null 2>&1
        if ($LASTEXITCODE -eq 0) {
            $counts = (& git rev-list --left-right --count "$Integration...$Branch") 2>$null
            if ($counts) {
                $parts = $counts -split '\s+'
                $behind = $parts[0]
                $ahead = $parts[1]
                # Behind is worth flagging: a task that is behind needs a sync
                # before its PR is mergeable, and finding that out at push time
                # is late.
                if ($behind -ne '0') { $ahead = "$ahead (behind $behind)" }
            }
        }
    }

    $dirty = "no"
    $status = (& git -C $Path status --porcelain) 2>$null
    if ($status) { $dirty = "yes" }

    Write-Host ($fmt -f $name, $Branch, $state, $ahead, $dirty)
}

foreach ($line in (& git worktree list --porcelain)) {
    if ($line -match '^worktree (.+)$') {
        if ($path) { Write-HarnessRow -Path $path -Branch $branch -Detached $detached -Integration $integration }
        $path = $Matches[1]
        $branch = ""
        $detached = $false
    }
    elseif ($line -match '^branch refs/heads/(.+)$') {
        $branch = $Matches[1]
    }
    elseif ($line -eq 'detached') {
        $detached = $true
        $branch = "(detached)"
    }
}
if ($path) { Write-HarnessRow -Path $path -Branch $branch -Detached $detached -Integration $integration }

Write-Host ""
Write-Host "Integration branch: $integration"
Write-Host ""
Write-Host "STATE 'detached' means the worktree is not on a branch and its commits"
Write-Host "are reachable only from the reflog. That is almost always a mistake."
Write-Host ""
Write-Host "AHEAD shows commits on the task branch that $integration does not have."
Write-Host "'(behind N)' means $integration has moved on; run wave-sync <task>."
