# ---------------------------------------------------------------------------
# wave-done <task>
#
# Finish a task: check its territory, build it, test it, check for forbidden
# files, push the branch, and open a pull request into the integration branch.
#
# The order matters. Territory and forbidden-file checks run first because
# they are cheap and they fail for reasons the author can fix immediately.
# The build runs last of the local checks because it is the slow one.
#
# Usage:
#   scripts/harness/wave-done.ps1 -Task canvas
#   scripts/harness/wave-done.ps1 -Task canvas -Draft
# ---------------------------------------------------------------------------

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Task,

    # Open the pull request as a draft.
    [switch] $Draft,

    # Skip configure/build/test, for documentation-only tasks.
    [switch] $NoBuild
)

$ErrorActionPreference = 'Stop'

. "$PSScriptRoot/lib/harness.ps1"

$repoRoot = Get-HarnessRepoRoot
Set-Location $repoRoot

if (-not (Assert-HarnessDev)) { exit 1 }

$worktree = Get-HarnessWorktreeForTask -Task $Task
if (-not $worktree) {
    Write-Error "no worktree found for task '$Task'. Run wave-list to see the active tasks."
    exit 1
}

$branch = (& git -C $worktree rev-parse --abbrev-ref HEAD) 2>$null
if ($branch -eq 'HEAD') {
    Write-Error "worktree for '$Task' is in detached HEAD state."
    exit 1
}

$integration = Get-HarnessIntegrationBranch

# The slug is the worktree directory name, which is also the build directory
# name inside it.
$slug = Split-Path -Leaf $worktree
$wave = ($slug -split '-')[0]

Write-Host "Finishing task $Task ($branch)"
Write-Host ""

# --- 1. territory ----------------------------------------------------------
#
# The declaration is required to exist and to be non-empty, and every file the
# branch touched must fall inside it. This runs before the build because a
# violation is a design problem, not a code problem, and no amount of passing
# tests makes it acceptable.

Write-Host "--- territory"
$taskFile = Get-HarnessTaskFile -Wave $wave -Task $Task
if (Test-Path $taskFile) {
    if (-not (Test-HarnessTaskChanges -Wave $wave -Task $Task -Worktree $worktree)) { exit 1 }
    Write-Host "ok"
} else {
    Write-Error @"
cannot find tasks/wave-$wave/$Task.md

The worktree is named '$slug'. If the wave or task name differs, run
wave-done with the task name that matches its declaration file.
"@
    exit 1
}

# --- 2. forbidden files ----------------------------------------------------
#
# Checked against the full branch diff, not just the last commit, because a
# forbidden file added in an earlier commit and deleted in a later one is
# still in the history that the pull request would merge.

Write-Host "--- forbidden files"
$paths = New-Object System.Collections.Generic.List[string]

$committed = (& git -C $worktree diff --name-only "$integration...HEAD") 2>$null
if ($committed) { foreach ($p in $committed) { if ($p) { $paths.Add($p) } } }

$staged = (& git -C $worktree diff --cached --name-only --diff-filter=ACMR) 2>$null
if ($staged) { foreach ($p in $staged) { if ($p) { $paths.Add($p) } } }

$unstaged = (& git -C $worktree diff --name-only --diff-filter=ACMR) 2>$null
if ($unstaged) { foreach ($p in $unstaged) { if ($p) { $paths.Add($p) } } }

if ($paths.Count -gt 0) {
    $unique = $paths | Sort-Object -Unique
    # Run the shell checker so there is exactly one implementation of the rule
    # rather than two that can drift apart. Git Bash ships with Git for
    # Windows, which this project already requires.
    $unique | & bash "$repoRoot/scripts/check-forbidden.sh"
    if ($LASTEXITCODE -ne 0) { exit 1 }
}
Write-Host "ok"

# --- 3. build and test -----------------------------------------------------

if ($NoBuild) {
    Write-Host "--- build skipped (-NoBuild)"
} else {
    Write-Host "--- build and test"
    if (-not (Invoke-HarnessBuildAndTest -Worktree $worktree -Slug $slug)) { exit 1 }
}

# --- 4. push ---------------------------------------------------------------
#
# Everything is committed at this point or the build would have been
# meaningless, so refuse a dirty tree rather than pushing a branch whose
# contents differ from what was just tested.

Write-Host "--- push"
$status = (& git -C $worktree status --porcelain) 2>$null
if ($status) {
    Write-Error @"
worktree has uncommitted changes

Commit them first. The build just ran against the working tree, and a
pushed branch must match what was tested.
"@
    exit 1
}

& git ls-remote --exit-code --heads origin $branch > $null 2>&1
if ($LASTEXITCODE -eq 0) {
    & git -C $worktree push --force-with-lease origin $branch
} else {
    & git -C $worktree push --set-upstream origin $branch
}
if ($LASTEXITCODE -ne 0) { throw "push failed" }

# --- 5. pull request -------------------------------------------------------

Write-Host "--- pull request"

$existing = (& gh pr list --head $branch --base $integration --json number --jq '.[0].number') 2>$null
if ($existing) {
    Write-Host "Pull request #$existing already exists for $branch"
    & gh pr view $existing --web
    exit 0
}

# The body is generated from the task declaration so the PR states what the
# task promised, not just what changed. A reviewer comparing the two is the
# point of having declared acceptance criteria in the first place.
$title = (Get-Content -LiteralPath $taskFile -TotalCount 1) -replace '^#\s*', ''
if (-not $title) { $title = "wave ${wave}: ${Task}" }

$bodyFile = [System.IO.Path]::GetTempFileName()
try {
    $body = New-Object System.Collections.Generic.List[string]
    $body.Add("Task: ``tasks/wave-$wave/$Task.md``")
    $body.Add("")
    $body.Add("## Declared territory")
    $body.Add("")
    foreach ($t in (Get-HarnessTaskTerritory -TaskFile $taskFile)) {
        $body.Add("- ``$t``")
    }
    $body.Add("")
    $body.Add("## Declaration")
    $body.Add("")
    # Everything below the first heading, so the PR carries the task's own
    # description, acceptance criteria and test plan.
    $rest = Get-Content -LiteralPath $taskFile | Select-Object -Skip 1
    foreach ($l in $rest) { $body.Add($l) }

    Set-Content -LiteralPath $bodyFile -Value $body -Encoding UTF8

    $prArgs = @('pr', 'create', '--base', $integration, '--head', $branch,
                '--title', $title, '--body-file', $bodyFile)
    if ($Draft) { $prArgs += '--draft' }

    & gh @prArgs
} finally {
    Remove-Item -LiteralPath $bodyFile -Force -ErrorAction SilentlyContinue
}
