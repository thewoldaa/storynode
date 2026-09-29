#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Install the forbidden-file check as a pre-commit hook.
#
# Run once after cloning:
#   bash scripts/install-hooks.sh
#
# The hook is a convenience for fast feedback. CI runs the same check and is
# the gate that actually holds, because a local hook can be skipped with
# --no-verify and CI cannot.
#
# The hook is not committed (git does not version .git/hooks), so this script
# is how it gets onto a fresh clone, and re-running it is harmless.
# ---------------------------------------------------------------------------

set -eu

repo_root=$(git rev-parse --show-toplevel)
hook_dir="$repo_root/.git/hooks"
hook_path="$hook_dir/pre-commit"

if [ ! -d "$hook_dir" ]; then
  echo "error: $hook_dir does not exist; is this a git repository?" >&2
  exit 1
fi

# Worktrees share the main repository's hooks directory through
# $GIT_COMMON_DIR, so installing once covers every worktree the harness
# creates. Say so, because otherwise it looks like the hook is missing when
# a task worktree is created.
common_dir=$(git rev-parse --git-common-dir)
if [ "$common_dir" != ".git" ] && [ -d "$common_dir/hooks" ]; then
  hook_dir="$common_dir/hooks"
  hook_path="$hook_dir/pre-commit"
fi

cat > "$hook_path" <<'HOOK'
#!/usr/bin/env bash
# Installed by scripts/install-hooks.sh - do not edit here, edit the source.
#
# Rejects a commit that stages a file which must never be public. Runs the
# shell checker directly rather than the PowerShell one so the hook works
# under Git Bash, WSL and Linux alike.
set -u

repo_root=$(git rev-parse --show-toplevel)

git diff --cached --name-only --diff-filter=ACMR \
  | bash "$repo_root/scripts/check-forbidden.sh"
HOOK

chmod +x "$hook_path"

echo "Installed pre-commit hook: $hook_path"
echo ""
echo "It rejects commits that stage forbidden files. To verify it works:"
echo "   touch CLAUDE.md && git add CLAUDE.md && git commit -m test"
echo "The commit should be refused. Then: git restore --staged CLAUDE.md"
