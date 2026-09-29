#!/usr/bin/env bash
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
#   scripts/harness/wave-sync.sh canvas
# ---------------------------------------------------------------------------

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=lib/harness.sh
. "$script_dir/lib/harness.sh"

usage() {
  cat <<'USAGE'
usage: wave-sync.sh <task>

Rebases the task's worktree onto the integration branch. Only that worktree
is touched.
USAGE
}

if [ "$#" -ne 1 ]; then
  usage >&2
  exit 2
fi

task="$1"
repo_root=$(harness_repo_root)
cd "$repo_root"

harness_require_dev

worktree=$(harness_worktree_for_task "$task") || {
  echo "error: no worktree found for task '$task'" >&2
  echo "Run wave-list.sh to see the active tasks." >&2
  exit 1
}

integration=$(harness_integration_branch)

# Refuse on a dirty tree rather than stashing. A rebase that silently stashes
# a task's half-finished work and fails to restore it is worse than refusing.
harness_require_clean_tree "$worktree"

current=$(git -C "$worktree" rev-parse --abbrev-ref HEAD)
if [ "$current" = "HEAD" ]; then
  echo "error: worktree for '$task' is in detached HEAD state" >&2
  echo "Check it out onto its branch first." >&2
  exit 1
fi

# Bring the integration branch up to date first, so a sync picks up work that
# was merged remotely and not just locally.
if git remote get-url origin >/dev/null 2>&1; then
  git fetch origin "$integration" --quiet
  if git rev-parse --verify --quiet "refs/remotes/origin/$integration" >/dev/null; then
    # Fast-forward only. If the local integration branch has diverged from the
    # remote, that is a problem for a human to look at, not something to
    # resolve automatically in a script that is meant to be safe.
    if git merge-base --is-ancestor "$integration" "origin/$integration" 2>/dev/null; then
      git branch -f "$integration" "origin/$integration"
    fi
  fi
fi

echo "Rebasing $current onto $integration"
echo "  worktree $worktree"
echo ""

if git -C "$worktree" rebase "$integration"; then
  echo ""
  echo "Rebased cleanly."
else
  echo "" >&2
  echo "Rebase stopped with conflicts. Resolve them in the worktree:" >&2
  echo "  cd \"$worktree\"" >&2
  echo "  git status" >&2
  echo "  git add <resolved files>" >&2
  echo "  git rebase --continue" >&2
  echo "" >&2
  echo "To give up and return to where you started:" >&2
  echo "  git rebase --abort" >&2
  exit 1
fi
