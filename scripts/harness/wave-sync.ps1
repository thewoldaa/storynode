# ---------------------------------------------------------------------------
# wave-sync <task>
#
# Rebase one task's worktree onto the integration branch.
#
# Touches exactly one worktree. Other tasks keep building against the commits
# they already have, which is what makes a sync safe to run while the rest of
# a wave is still working.
#
# Usage:
#   scripts/harness/wave-sync.ps1 -Task canvas
# ---------------------------------------------------------------------------

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Task
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

$integration = Get-HarnessIntegrationBranch

# Refuse on a dirty tree rather than stashing. A rebase that silently stashes
# a task's half-finished work and fails to restore it is worse than refusing.
if (-not (Assert-HarnessCleanTree -Where $worktree)) { exit 1 }

$current = (& git -C $worktree rev-parse --abbrev-ref HEAD) 2>$null
if ($current -eq 'HEAD') {
    Write-Error "worktree for '$Task' is in detached HEAD state. Check it out onto its branch first."
    exit 1
}

# Bring the integration branch up to date first, so a sync picks up work that
# was merged remotely and not just locally.
& git remote get-url origin > $null 2>&1
if ($LASTEXITCODE -eq 0) {
    & git fetch origin $integration --quiet
    & git rev-parse --verify --quiet "refs/remotes/origin/$integration" > $null 2>&1
    if ($LASTEXITCODE -eq 0) {
        # Fast-forward only. If the local integration branch has diverged from
        # the remote, that is a problem for a human to look at, not something
        # to resolve automatically in a script that is meant to be safe.
        & git merge-base --is-ancestor $integration "origin/$integration" 2>$null
        if ($LASTEXITCODE -eq 0) {
            & git branch -f $integration "origin/$integration"
        }
    }
}

Write-Host "Rebasing $current onto $integration"
Write-Host "  worktree $worktree"
Write-Host ""

& git -C $worktree rebase $integration
if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Rebased cleanly."
    exit 0
}

Write-Host ""
Write-Host "Rebase stopped with conflicts. Resolve them in the worktree:" -ForegroundColor Yellow
Write-Host "  cd `"$worktree`""
Write-Host "  git status"
Write-Host "  git add <resolved files>"
Write-Host "  git rebase --continue"
Write-Host ""
Write-Host "To give up and return to where you started:"
Write-Host "  git rebase --abort"
exit 1
