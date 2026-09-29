# ---------------------------------------------------------------------------
# Reject commits and PRs that contain files which must never be public.
#
# Windows counterpart of scripts/check-forbidden.sh. The two are kept
# behaviourally identical on purpose: the pre-commit hook uses this one on
# Windows, CI uses the shell one, and a divergence between them means a commit
# that passes locally and fails in CI, which is worse than either failing.
#
# Reads git path names from stdin, one per line, in the format produced by
#   git diff --cached --name-only --diff-filter=ACMR
# and exits non-zero if any path matches a forbidden pattern.
#
# Usage:
#   git diff --cached --name-only --diff-filter=ACMR | pwsh -File scripts/check-forbidden.ps1
#   pwsh -File scripts/check-forbidden.ps1 -Paths @("src/a.cpp", "CLAUDE.md")
# ---------------------------------------------------------------------------

[CmdletBinding()]
param(
    # Paths to check. When omitted, paths are read from stdin, which is how
    # the pre-commit hook calls this script.
    [Parameter(ValueFromPipeline = $true)]
    [string[]] $Paths
)

begin {
    # --- forbidden patterns ------------------------------------------------
    #
    # .NET regular expressions, matched against the whole path. Git stores
    # forward slashes on every platform, so the patterns assume that form and
    # never a Windows separator.
    #
    # Every pattern is anchored at a path-segment boundary so that
    # "^CLAUDE\.md$" does not fire on "docs/CLAUDE.md.txt" and "^\.claude/"
    # does not fire on "src/.claude-thing".
    $Patterns = @(
        # Agent instruction files: working notes, not project documentation.
        '(^|/)AGENTS\.md$'
        '(^|/)CLAUDE\.md$'
        '(^|/)\.claude/'
        '(^|/)\.agents/'
        '(^|/)\.cursor/'

        # Local scratch space. A worktree is a checkout of this repository;
        # committing one would duplicate the whole tree.
        '(^|/)\.worktrees/'
        '(^|/)agent-notes/'
        '(^|/)scratch/'
        '(^|/)personal/'

        # Credentials. Nothing here needs one to build or run.
        '(^|/)\.env$'
        '(^|/)\.env\..+'
        '(^|/)[^/]+\.local$'
        '(^|/)[^/]+\.key$'
        '(^|/)[^/]+\.pem$'
        '(^|/)secrets/'

        # Build output: large, machine-specific, regenerated everywhere.
        #
        # Anchored to the repository root, unlike the categories above. A
        # nested "build/" is a legitimate directory name inside a source tree
        # (src/app/build/ is a reasonable place for build-system glue), and
        # rejecting it would be a false positive that blocks real work. The
        # directories this rule exists to catch are always at the root:
        # build/<task> for the harness, build/ci for CI.
        '^build/'
        '^out/'
        '^\.vs/'
        '^\.idea/'
        '(^|/)CMakeUserPresets\.json$'
    )

    # --- explicit exceptions -----------------------------------------------
    #
    # A path listed here is allowed even when a pattern above matches it.
    # Keep the list short and justify every entry; this is the only place the
    # rule can be loosened, so an unexplained entry is a bug.
    $Allow = @(
        # The checker itself contains the forbidden strings as patterns and
        # has to be committed for CI to run it.
        '^scripts/check-forbidden\.sh$'
        '^scripts/check-forbidden\.ps1$'
        # The ignore file and this project's own documentation necessarily
        # name the forbidden paths.
        '^\.gitignore$'
        '^CONTRIBUTING\.md$'
        '^docs/'
        '^tasks/'
    )

    $Collected = [System.Collections.Generic.List[string]]::new()
}

process {
    if ($Paths) {
        foreach ($p in $Paths) { if ($p) { $Collected.Add($p) } }
    }
    elseif ($MyInvocation.ExpectingInput) {
        foreach ($p in $input) { if ($p) { $Collected.Add($p) } }
    }
}

end {
    if (-not $MyInvocation.ExpectingInput -and -not $Paths) {
        $stdin = [Console]::In.ReadToEnd()
        if ($stdin) {
            foreach ($line in ($stdin -split "`r?`n")) {
                if ($line.Trim()) { $Collected.Add($line.Trim()) }
            }
        }
    }

    if ($Collected.Count -eq 0) { exit 0 }

    $Violations = [System.Collections.Generic.List[string]]::new()

    foreach ($rawPath in $Collected) {
        # git quotes paths containing unusual characters; strip the quotes so
        # the patterns see the real name.
        $path = $rawPath.Trim().Trim('"')
        if (-not $path) { continue }

        $allowed = $false
        foreach ($a in $Allow) {
            if ($path -match $a) { $allowed = $true; break }
        }
        if ($allowed) { continue }

        foreach ($pattern in $Patterns) {
            if ($path -match $pattern) {
                $Violations.Add($path)
                break
            }
        }
    }

    if ($Violations.Count -gt 0) {
        Write-Host ""
        Write-Host "=============================================================="
        Write-Host " FORBIDDEN FILES DETECTED - commit refused"
        Write-Host "=============================================================="
        Write-Host ""
        Write-Host "These paths must never enter this repository:"
        Write-Host ""
        foreach ($v in $Violations) { Write-Host "   $v" }
        Write-Host ""
        Write-Host "Fix it with one of:"
        Write-Host "   git restore --staged <path>        keep the file, unstage it"
        Write-Host "   git rm --cached <path>             keep the file, untrack it"
        Write-Host "   Remove-Item <path>                 delete it"
        Write-Host ""
        Write-Host "If the file is already committed, the history needs rewriting,"
        Write-Host "not just a new commit that deletes it."
        Write-Host "=============================================================="
        Write-Host ""
        exit 1
    }

    exit 0
}
