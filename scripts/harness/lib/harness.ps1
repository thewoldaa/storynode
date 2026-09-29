# ---------------------------------------------------------------------------
# Shared functions for the wave harness (Windows).
#
# Windows counterpart of scripts/harness/lib/harness.sh. The two implement the
# same contract and are kept behaviourally identical on purpose: a task may be
# started from PowerShell and finished from Bash, and a divergence between the
# two would make the harness's guarantees depend on which shell was used.
#
# Dot-source it:
#   . "$PSScriptRoot/lib/harness.ps1"
#
# The harness exists to make parallel work safe. Three rules do the work:
#
#   1. One task, one branch, one worktree, one build directory.
#   2. A task declares its file territory in tasks/wave-<wave>/<task>.md, and
#      no two tasks in the same wave may declare overlapping territory.
#   3. A task's commits must fall inside its own declared territory.
#
# Rule 2 is checked before a worktree is created, because a conflict found
# then costs nothing and a conflict found after a day of parallel work costs
# the day. Rule 3 is checked before a push, because that is the last moment
# the mistake is still cheap to fix.
# ---------------------------------------------------------------------------

Set-StrictMode -Version Latest

# --- repository layout -----------------------------------------------------

function Get-HarnessRepoRoot {
    $root = (& git rev-parse --show-toplevel) 2>$null
    if ($LASTEXITCODE -ne 0) { throw "not inside a git repository" }
    # git returns forward slashes; normalise for Windows path handling.
    return ($root -replace '/', '\')
}

function Get-HarnessWorktreeRoot {
    return (Join-Path (Get-HarnessRepoRoot) '.worktrees')
}

function Get-HarnessIntegrationBranch {
    if ($env:STORYNODE_INTEGRATION_BRANCH) { return $env:STORYNODE_INTEGRATION_BRANCH }
    return 'dev'
}

# --- naming ----------------------------------------------------------------
#
# A task is identified by its wave and name, and everything derived from it
# uses the same slug so that branch, worktree, build directory and task file
# can all be found from the slug alone.

function Get-HarnessSlug {
    param([string] $Wave, [string] $Task)

    $raw = "$Wave-$Task".ToLowerInvariant()
    # Anything that is not alphanumeric, dot, dash or underscore becomes a
    # dash. Slashes are deliberately excluded: a slash in a task name would
    # make the build directory nested and the branch ambiguous.
    return ($raw -replace '[^a-z0-9._-]', '-')
}

function Get-HarnessBranch {
    param([string] $Wave, [string] $Task)
    return "wave/$(Get-HarnessSlug -Wave $Wave -Task $Task)"
}

function Get-HarnessWorktreePath {
    param([string] $Wave, [string] $Task)
    return (Join-Path (Get-HarnessWorktreeRoot) (Get-HarnessSlug -Wave $Wave -Task $Task))
}

function Get-HarnessBuildDir {
    param([string] $Wave, [string] $Task)
    return "build/$(Get-HarnessSlug -Wave $Wave -Task $Task)"
}

function Get-HarnessTaskFile {
    param([string] $Wave, [string] $Task)
    return (Join-Path (Get-HarnessRepoRoot) "tasks\wave-$Wave\$Task.md")
}

# Find the worktree for a task name alone, by scanning git's own worktree
# list. Used by sync/done/clean, which take only a task name because requiring
# the wave too would mean remembering it.
#
# The branch is "wave/<wave>-<task>" and the worktree directory is the same
# slug, so the match is on the segment after the final dash: the task name
# alone is what the caller has. Matching "wave-<task>" instead would miss
# every real case, because the slug always begins with the wave number.
function Get-HarnessWorktreeForTask {
    param([string] $Task)

    $path = $null

    foreach ($line in (& git worktree list --porcelain)) {
        if ($line -match '^worktree (.+)$') {
            $path = $Matches[1]
        }
        elseif ($line -match '^branch refs/heads/wave/(.+)$') {
            $slug = $Matches[1]
            # Compare the trailing "-<task>" segment exactly, so task "canvas"
            # does not match "canvas-layers" and "core" does not match
            # "core-ui".
            if ($slug.EndsWith("-$Task", [System.StringComparison]::Ordinal)) {
                return ($path -replace '/', '\')
            }
        }
    }

    return $null
}

# --- task declaration ------------------------------------------------------

function Assert-HarnessTaskFile {
    param([string] $Wave, [string] $Task)

    $file = Get-HarnessTaskFile -Wave $Wave -Task $Task
    if (-not (Test-Path $file)) {
        Write-Error @"
no task declaration at tasks/wave-$Wave/$Task.md

Create it first. It must contain a '## Territory' section listing the paths
this task owns, for example:

    ## Territory

    - src/ui/assets/canvas/**
    - src/ui/assets/styles/canvas.css

See tasks/wave-0/core.md for a worked example.
"@
        return $false
    }
    return $true
}

# Return the territory entries of a task file, one per line.
#
# Reads the lines under "## Territory" until the next "##" heading. Only lines
# beginning with "- " are taken, and inline comments after " #" are stripped,
# so the file stays readable.
function Get-HarnessTaskTerritory {
    param([string] $TaskFile)

    $inSection = $false
    $entries = New-Object System.Collections.Generic.List[string]

    foreach ($line in (Get-Content -LiteralPath $TaskFile)) {
        if ($line -match '^##\s+Territory\s*$') { $inSection = $true; continue }
        if ($line -match '^##\s+') { $inSection = $false; continue }
        if ($inSection -and $line -match '^\s*-\s+(.+)$') {
            $entry = $Matches[1]
            $entry = $entry -replace '\s+#.*$', ''
            $entry = $entry.Trim()
            if ($entry) { $entries.Add($entry) }
        }
    }

    return $entries
}

# --- territory validation --------------------------------------------------
#
# Territories are deliberately simple. A line is one of:
#
#   path/to/file.ext      exactly that file
#   path/to/dir/*         the direct children of that directory
#   path/to/dir/**        that directory and everything under it
#
# Wildcards anywhere else are rejected. Supporting them would mean
# implementing glob intersection, and the point of the check is to be
# obviously correct rather than expressive. If a task genuinely needs a
# pattern, it can declare the parent directory instead.

function Test-HarnessTerritoryLine {
    param([string] $Line)

    if ($Line -notmatch '^[A-Za-z0-9._/-]+(/\*\*|/\*)?$') { return $false }
    # Reject ".." segments: a territory must not escape the repository.
    if ($Line -match '(^|/)\.\.(/|$)') { return $false }
    return $true
}

function Get-HarnessTerritoryPrefix {
    param([string] $Line)
    return ($Line -replace '/\*\*$', '' -replace '/\*$', '')
}

function Test-HarnessTerritoryRecursive {
    param([string] $Line)

    # Ordinal suffix test, not -like. In PowerShell's -like operator "*" is a
    # wildcard, so '-like "*/**"' matches almost any path containing a slash
    # and would classify every single-level glob as recursive. That is exactly
    # the bug this comment exists to prevent.
    return $Line.EndsWith('/**', [System.StringComparison]::Ordinal)
}

# Do two territory lines overlap?
#
# Two entries conflict when one covers the other. Because every entry reduces
# to a directory prefix, this is a prefix test at a path boundary:
#
#   src/core/**          vs src/core/model.hpp   -> conflict (covered)
#   src/core/**          vs src/coreutils/x.hpp  -> no conflict (not a
#                                                   segment boundary)
#   src/a/*              vs src/a/b/c.hpp        -> no conflict (* is one
#                                                   level only)
function Test-HarnessTerritoriesOverlap {
    param([string] $A, [string] $B)

    $pa = Get-HarnessTerritoryPrefix -Line $A
    $pb = Get-HarnessTerritoryPrefix -Line $B

    if ($pa -eq $pb) { return $true }

    if ($pa.Length -lt $pb.Length) { $shorter = $pa; $longer = $pb }
    else { $shorter = $pb; $longer = $pa }

    if ($longer.StartsWith("$shorter/", [System.StringComparison]::Ordinal)) {
        # The deeper path is inside the shallower one. This only conflicts if
        # the shallower entry is recursive, or if the deeper entry is exactly
        # one level down (which "/*" covers).
        if ((Test-HarnessTerritoryRecursive -Line $A) -or (Test-HarnessTerritoryRecursive -Line $B)) {
            return $true
        }
        $rest = $longer.Substring($shorter.Length + 1)
        if ($rest -notmatch '/') { return $true }
        return $false
    }

    return $false
}

# Check every pair of territories in a wave for overlap.
function Test-HarnessWaveTerritories {
    param([string] $Wave)

    $tasksDir = Join-Path (Get-HarnessRepoRoot) "tasks\wave-$Wave"
    if (-not (Test-Path $tasksDir)) { return $true }

    $names = New-Object System.Collections.Generic.List[string]
    $territories = New-Object System.Collections.Generic.List[string]

    foreach ($f in (Get-ChildItem -LiteralPath $tasksDir -Filter '*.md' -File)) {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
        foreach ($line in (Get-HarnessTaskTerritory -TaskFile $f.FullName)) {
            $names.Add($name)
            $territories.Add($line)
        }
    }

    for ($i = 0; $i -lt $territories.Count; $i++) {
        for ($j = $i + 1; $j -lt $territories.Count; $j++) {
            # A task never conflicts with itself; the same task declaring a
            # directory and a file inside it is redundant but harmless.
            if ($names[$i] -eq $names[$j]) { continue }

            if (Test-HarnessTerritoriesOverlap -A $territories[$i] -B $territories[$j]) {
                Write-Error @"
territory conflict in wave $Wave

  tasks/wave-$Wave/$($names[$i]).md
    declares: $($territories[$i])
  tasks/wave-$Wave/$($names[$j]).md
    declares: $($territories[$j])

Two tasks in one wave must never write the same files. Move the shared
surface into a core task in its own wave, or narrow one of the two
territories.
"@
                return $false
            }
        }
    }

    return $true
}

# Does a repository-relative path fall inside a territory entry?
function Test-HarnessPathInTerritory {
    param([string] $Path, [string] $Territory)

    $prefix = Get-HarnessTerritoryPrefix -Line $Territory
    $p = $Path -replace '\\', '/'

    if ($p -eq $prefix) { return $true }

    if ($p.StartsWith("$prefix/", [System.StringComparison]::Ordinal)) {
        if (Test-HarnessTerritoryRecursive -Line $Territory) { return $true }
        $rest = $p.Substring($prefix.Length + 1)
        if ($rest -notmatch '/') { return $true }
        return $false
    }

    return $false
}

# Check that every file a task has touched falls inside its territory.
function Test-HarnessTaskChanges {
    param([string] $Wave, [string] $Task, [string] $Worktree)

    $file = Get-HarnessTaskFile -Wave $Wave -Task $Task
    $territories = Get-HarnessTaskTerritory -TaskFile $file

    if ($territories.Count -eq 0) {
        Write-Error "tasks/wave-$Wave/$Task.md declares no territory"
        return $false
    }

    $integration = Get-HarnessIntegrationBranch

    $changed = New-Object System.Collections.Generic.List[string]

    # Files this branch changed relative to where it forked from dev.
    $diff = (& git -C $Worktree diff --name-only "$integration...HEAD") 2>$null
    if ($LASTEXITCODE -eq 0 -and $diff) {
        foreach ($f in $diff) { if ($f) { $changed.Add(($f -replace '\\', '/')) } }
    }

    # Uncommitted work counts too: wave-done runs before the final commit in
    # some workflows, and a violation that is only visible after committing is
    # a violation found too late.
    $status = (& git -C $Worktree status --porcelain) 2>$null
    if ($status) {
        foreach ($line in $status) {
            if ($line.Length -gt 3) {
                $f = $line.Substring(3).Trim()
                # Renames are reported as "old -> new"; the new path is what
                # the branch now contains.
                if ($f -match ' -> (.+)$') { $f = $Matches[1] }
                $f = $f.Trim('"')
                $changed.Add(($f -replace '\\', '/'))
            }
        }
    }

    if ($changed.Count -eq 0) { return $true }

    $outside = New-Object System.Collections.Generic.List[string]
    foreach ($f in $changed) {
        $inside = $false
        foreach ($t in $territories) {
            if (Test-HarnessPathInTerritory -Path $f -Territory $t) { $inside = $true; break }
        }
        if (-not $inside) { $outside.Add($f) }
    }

    if ($outside.Count -gt 0) {
        Write-Host ""
        Write-Host "error: task $Wave/$Task edited files outside its territory" -ForegroundColor Red
        Write-Host ""
        Write-Host "  declared territory:"
        foreach ($t in $territories) { Write-Host "    $t" }
        Write-Host ""
        Write-Host "  files outside it:"
        foreach ($f in $outside) { Write-Host "    $f" }
        Write-Host ""
        Write-Host "Either move the change into a task that owns those files, or widen"
        Write-Host "this task's territory in tasks/wave-$Wave/$Task.md and re-run."
        return $false
    }

    return $true
}

# --- tool lookup ------------------------------------------------------------
#
# PowerShell usually inherits the machine PATH, but a tool installed after the
# shell started is not visible until it is restarted. Look in the standard
# install locations before giving up, so a fresh session works rather than
# failing on a machine where the tool plainly exists.

function Find-HarnessTool {
    param(
        [string] $Name,
        [string[]] $Candidates
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }

    return $null
}

function Get-HarnessCMake {
    $found = Find-HarnessTool -Name cmake -Candidates @(
        "$env:ProgramFiles\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe",
        "$env:LOCALAPPDATA\Programs\CMake\bin\cmake.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    if (-not $found) {
        Write-Error "cmake not found. Install it with: winget install Kitware.CMake"
        return $null
    }
    return $found
}

function Get-HarnessCTest {
    $found = Find-HarnessTool -Name ctest -Candidates @(
        "$env:ProgramFiles\CMake\bin\ctest.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\ctest.exe",
        "$env:LOCALAPPDATA\Programs\CMake\bin\ctest.exe"
    )
    if (-not $found) {
        Write-Error "ctest not found. It ships with CMake."
        return $null
    }
    return $found
}

# --- build and test --------------------------------------------------------

# Configure, build and test inside a worktree.
#
# The build directory is per-task so parallel builds never share an object
# file or a CMake cache. Everything is done through the worktree's own copy
# of the sources, so a build here cannot see another task's changes.
function Invoke-HarnessBuildAndTest {
    param([string] $Worktree, [string] $Slug)

    $cmake = Get-HarnessCMake
    if (-not $cmake) { return $false }
    $ctest = Get-HarnessCTest
    if (-not $ctest) { return $false }

    $buildDir = Join-Path $Worktree "build\$Slug"
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    Write-Host "--- configure ($Slug)"
    & $cmake -S $Worktree -B $buildDir -G "Visual Studio 17 2022" -A x64 -DSTORYNODE_BUILD_TESTS=ON *> (Join-Path $buildDir 'configure.log')
    if ($LASTEXITCODE -ne 0) {
        Write-Host "configure failed; see $(Join-Path $buildDir 'configure.log')" -ForegroundColor Red
        Get-Content (Join-Path $buildDir 'configure.log') -Tail 40 | ForEach-Object { Write-Host $_ }
        return $false
    }

    Write-Host "--- build ($Slug)"
    & $cmake --build $buildDir --config Debug --parallel *> (Join-Path $buildDir 'build.log')
    if ($LASTEXITCODE -ne 0) {
        Write-Host "build failed; see $(Join-Path $buildDir 'build.log')" -ForegroundColor Red
        Get-Content (Join-Path $buildDir 'build.log') -Tail 60 | ForEach-Object { Write-Host $_ }
        return $false
    }

    Write-Host "--- test ($Slug)"
    & $ctest --test-dir $buildDir -C Debug --output-on-failure *> (Join-Path $buildDir 'test.log')
    if ($LASTEXITCODE -ne 0) {
        Write-Host "tests failed; see $(Join-Path $buildDir 'test.log')" -ForegroundColor Red
        Get-Content (Join-Path $buildDir 'test.log') -Tail 60 | ForEach-Object { Write-Host $_ }
        return $false
    }

    Write-Host "--- ok"
    return $true
}

# --- misc ------------------------------------------------------------------

function Assert-HarnessCleanTree {
    param([string] $Where = (Get-Location).Path)

    $status = (& git -C $Where status --porcelain) 2>$null
    if ($status) {
        Write-Error "working tree at $Where has uncommitted changes. Commit or stash them first."
        return $false
    }
    return $true
}

function Assert-HarnessDev {
    $integration = Get-HarnessIntegrationBranch
    & git rev-parse --verify --quiet $integration > $null 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Error @"
integration branch '$integration' does not exist

Create it from main first:
    git branch $integration main
"@
        return $false
    }
    return $true
}
