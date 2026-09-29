# ---------------------------------------------------------------------------
# Install the forbidden-file check as a pre-commit hook (Windows).
#
# Run once after cloning:
#   powershell -ExecutionPolicy Bypass -File scripts/install-hooks.ps1
#
# Windows counterpart of scripts/install-hooks.sh. Either one produces the
# same hook; use whichever matches your shell.
#
# The hook is not committed (git does not version .git/hooks), so this script
# is how it gets onto a fresh clone, and re-running it is harmless.
# ---------------------------------------------------------------------------

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (& git rev-parse --show-toplevel)
if ($LASTEXITCODE -ne 0) { throw "not inside a git repository" }

# Worktrees share the main repository's hooks directory through the common
# git dir, so installing once covers every worktree the harness creates.
# Without this check the hook would land in a worktree-local hooks folder that
# nothing consults, and commits from that worktree would go unchecked.
$commonDir = (& git rev-parse --git-common-dir)
if ($commonDir -ne '.git' -and (Test-Path (Join-Path $commonDir 'hooks'))) {
    $hookDir = Join-Path $commonDir 'hooks'
} else {
    $hookDir = Join-Path $repoRoot '.git\hooks'
}

if (-not (Test-Path $hookDir)) {
    throw "$hookDir does not exist; is this a git repository?"
}

$hookPath = Join-Path $hookDir 'pre-commit'

# The hook shells out to Git Bash, which ships with Git for Windows and is
# present on any machine that can run the rest of this project. Invoking the
# checker through bash keeps one implementation authoritative rather than
# maintaining a second one in PowerShell.
$hookBody = @'
#!/usr/bin/env bash
# Installed by scripts/install-hooks.ps1 - do not edit here, edit the source.
#
# Rejects a commit that stages a file which must never be public.
set -u

repo_root=$(git rev-parse --show-toplevel)

git diff --cached --name-only --diff-filter=ACMR \
  | bash "$repo_root/scripts/check-forbidden.sh"
'@

# LF line endings, no BOM: a bash script with a UTF-8 BOM fails with
# "command not found" on the shebang line, and CRLF breaks it on Linux.
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
$hookBodyLf = $hookBody -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($hookPath, $hookBodyLf, $utf8NoBom)

Write-Host "Installed pre-commit hook: $hookPath"
Write-Host ""
Write-Host "It rejects commits that stage forbidden files. To verify it works:"
Write-Host "   New-Item CLAUDE.md; git add CLAUDE.md; git commit -m test"
Write-Host "The commit should be refused. Then: git restore --staged CLAUDE.md"
