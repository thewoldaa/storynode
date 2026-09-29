# ---------------------------------------------------------------------------
# wave-clean <task>
#
# Remove a finished task's worktree and delete its branch, locally and on the
# remote.
#
# Refuses unless the pull request is actually merged. Deleting a branch whose
# work was not merged loses commits that exist nowhere else, and the branch is
# the only place they live once the worktree is gone.
#
# Usage:
#   scripts/harness/wave-clean.ps1 -Task canvas
#   scripts/harness/wave-clean.ps1 -Task canvas -Force
# ---------------------------------------------------------------------------

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Task,

    # Remove the worktree even if it has uncommitted changes.
    [switch] $Force
)

$ErrorActionPreference = 'Stop'

. "$PSScriptRoot/lib/harness.ps1"

$repoRoot = Get-HarnessRepoRoot
Set-Location $repoRoot

$worktree = Get-HarnessWorktreeForTask -Task $Task
if (-not $worktree) {
    Write-Error "no worktree found for task '$Task'. Run wave-list to see the active tasks."
    exit 1
}

$branch = (& git -C $worktree rev-parse --abbrev-ref HEAD) 2>$null
if ($branch -eq 'HEAD' -or -not $branch) {
    Write-Error @"
worktree for '$Task' is in detached HEAD state

Its commits may be reachable only from the reflog. Inspect it before removing:
  cd "$worktree" ; git log --oneline -20
"@
    exit 1
}

$integration = Get-HarnessIntegrationBranch

# --- is the work actually merged? ------------------------------------------

$merged = $false

# A branch whose commits are all reachable from the integration branch is
# merged, however it got there: a PR merge, a fast-forward, or a rebase.
& git rev-parse --verify --quiet $integration > $null 2>&1
if ($LASTEXITCODE -eq 0) {
    & git merge-base --is-ancestor $branch $integration 2>$null
    if ($LASTEXITCODE -eq 0) { $merged = $true }
}

# Otherwise ask GitHub. A squash merge rewrites the commits, so the ancestry
# test above fails even though the work is merged and the branch is safe to
# delete.
if (-not $merged) {
    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if ($gh) {
        $state = (& gh pr list --head $branch --state all --json state --jq '.[0].state') 2>$null
        if ($state -eq 'MERGED') {
            $merged = $true
        } elseif ($state) {
            Write-Error @"
pull request for '$branch' is $state, not merged

Cleaning up now would delete commits that exist nowhere else.
Merge or close the pull request first:
  gh pr view --head $branch --web
"@
            exit 1
        }
    }
}

if (-not $merged) {
    Write-Error @"
'$branch' is not merged into '$integration'

There is no pull request for it, or the pull request is still open.
If the work is genuinely finished elsewhere and this branch is obsolete,
delete it deliberately:
  git worktree remove --force "$worktree"
  git branch -D "$branch"
  git push origin --delete "$branch"
"@
    exit 1
}

# --- remove -----------------------------------------------------------------

$status = (& git -C $worktree status --porcelain) 2>$null
if (-not $Force -and $status) {
    Write-Host "error: worktree for '$Task' has uncommitted changes" -ForegroundColor Red
    Write-Host ""
    & git -C $worktree status --short | ForEach-Object { Write-Host $_ }
    Write-Host ""
    Write-Host "Commit them, stash them, or discard them, then re-run."
    Write-Host "To discard them and remove the worktree anyway: -Force"
    exit 1
}

Write-Host "Removing worktree $worktree"
if ($Force) {
    & git worktree remove --force $worktree
} else {
    & git worktree remove $worktree
}
if ($LASTEXITCODE -ne 0) { throw "git worktree remove failed" }

# Build output lives inside the worktree and goes with it, but a stray
# directory can survive if the worktree was removed by hand.
$strayBuild = Join-Path $repoRoot ("build\" + (Split-Path -Leaf $worktree))
if (Test-Path $strayBuild) {
    Write-Host "Removing stray build directory $strayBuild"
    Remove-Item -Recurse -Force $strayBuild
}

Write-Host "Deleting local branch $branch"
& git branch -D $branch

& git ls-remote --exit-code --heads origin $branch > $null 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "Deleting remote branch $branch"
    & git push origin --delete $branch
} else {
    Write-Host "Remote branch $branch does not exist; nothing to delete."
}

# Worktrees leave administrative files behind when removed abruptly, and
# prune is the supported way to clear them.
& git worktree prune

Write-Host ""
Write-Host "Cleaned up task '$Task'."
