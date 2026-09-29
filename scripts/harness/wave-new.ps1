# ---------------------------------------------------------------------------
# wave-new <wave> <task>
#
# Start a task: verify its declaration, create its branch from the latest
# integration branch, add a worktree, and prepare its build directory.
#
# Refuses to start if another task in the same wave declares overlapping file
# territory. That check is the whole point of the harness: a conflict found
# now costs nothing, and the same conflict found after a day of parallel work
# costs the day.
#
# Usage:
#   scripts/harness/wave-new.ps1 -Wave 1 -Task canvas
#   scripts/harness/wave-new.ps1 0 core
# ---------------------------------------------------------------------------

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Wave,

    [Parameter(Mandatory = $true, Position = 1)]
    [string] $Task
)

$ErrorActionPreference = 'Stop'

. "$PSScriptRoot/lib/harness.ps1"

if ($Wave -notmatch '^[0-9]+$') {
    Write-Error "wave must be a number, got '$Wave'"
    exit 2
}

$repoRoot = Get-HarnessRepoRoot
Set-Location $repoRoot

if (-not (Assert-HarnessDev)) { exit 1 }
if (-not (Assert-HarnessTaskFile -Wave $Wave -Task $Task)) { exit 1 }

$slug     = Get-HarnessSlug      -Wave $Wave -Task $Task
$branch   = Get-HarnessBranch    -Wave $Wave -Task $Task
$worktree = Get-HarnessWorktreePath -Wave $Wave -Task $Task
$buildRel = Get-HarnessBuildDir  -Wave $Wave -Task $Task

# --- refusals before anything is created -----------------------------------

& git rev-parse --verify --quiet "refs/heads/$branch" > $null 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Error "branch '$branch' already exists. Finish it with wave-done, or remove it with wave-clean."
    exit 1
}

if (Test-Path $worktree) {
    Write-Error "worktree '$worktree' already exists. Remove it with wave-clean, or pick another task name."
    exit 1
}

# --- territory check -------------------------------------------------------

Write-Host "Checking wave $Wave territory..."
if (-not (Test-HarnessWaveTerritories -Wave $Wave)) { exit 1 }

# --- create ----------------------------------------------------------------

$integration = Get-HarnessIntegrationBranch

# Branch from the remote integration branch when one exists, so a task never
# starts from a stale local copy. Falling back to the local branch keeps the
# harness usable before the first push.
$base = $integration
& git rev-parse --verify --quiet "refs/remotes/origin/$integration" > $null 2>&1
if ($LASTEXITCODE -eq 0) {
    & git fetch origin $integration --quiet
    $base = "origin/$integration"
}

Write-Host "Creating $branch from $base"
& git worktree add -b $branch $worktree $base --quiet
if ($LASTEXITCODE -ne 0) { throw "git worktree add failed" }

# The build directory lives inside the worktree so parallel builds never share
# an object file or a CMake cache. It is git-ignored by the "build/" rule.
New-Item -ItemType Directory -Force -Path (Join-Path $worktree ($buildRel -replace '/', '\')) | Out-Null

Write-Host ""
Write-Host "Ready."
Write-Host ""
Write-Host "  worktree   $worktree"
Write-Host "  branch     $branch"
Write-Host "  build dir  $buildRel"
Write-Host ""
Write-Host "  territory  (from tasks/wave-$Wave/$Task.md)"
foreach ($line in (Get-HarnessTaskTerritory -TaskFile (Get-HarnessTaskFile -Wave $Wave -Task $Task))) {
    Write-Host "             $line"
}
Write-Host ""
Write-Host "Next:"
Write-Host "  cd `"$worktree`""
Write-Host "  scripts/fetch-deps.ps1        # once per worktree"
Write-Host ""
Write-Host "Edit only inside the territory above. When the task is finished:"
Write-Host "  scripts/harness/wave-done.ps1 -Task $Task"
