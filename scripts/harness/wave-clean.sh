#!/usr/bin/env bash
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
#   scripts/harness/wave-clean.sh canvas
#   scripts/harness/wave-clean.sh canvas --force
# ---------------------------------------------------------------------------

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=lib/harness.sh
. "$script_dir/lib/harness.sh"

usage() {
  cat <<'USAGE'
usage: wave-clean.sh <task> [--force]

  --force   remove the worktree even if it has uncommitted changes

Refuses unless the task's pull request is merged, or its branch is already
contained in the integration branch.
USAGE
}

force=0
task=""

for arg in "$@"; do
  case "$arg" in
    --force)   force=1 ;;
    -h|--help) usage; exit 0 ;;
    *)         task="$arg" ;;
  esac
done

if [ -z "$task" ]; then
  usage >&2
  exit 2
fi

repo_root=$(harness_repo_root)
cd "$repo_root"

worktree=$(harness_worktree_for_task "$task") || {
  echo "error: no worktree found for task '$task'" >&2
  echo "Run wave-list.sh to see the active tasks." >&2
  exit 1
}

branch=$(git -C "$worktree" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "")
if [ "$branch" = "HEAD" ] || [ -z "$branch" ]; then
  echo "error: worktree for '$task' is in detached HEAD state" >&2
  echo "Its commits may be reachable only from the reflog." >&2
  echo "Inspect it before removing:" >&2
  echo "  cd \"$worktree\" && git log --oneline -20" >&2
  exit 1
fi

integration=$(harness_integration_branch)

# --- is the work actually merged? ------------------------------------------

merged=0

# A branch whose commits are all reachable from the integration branch is
# merged, however it got there: a PR merge, a fast-forward, or a rebase.
if git rev-parse --verify --quiet "$integration" >/dev/null 2>&1; then
  if git merge-base --is-ancestor "$branch" "$integration" 2>/dev/null; then
    merged=1
  fi
fi

# Otherwise ask GitHub. A squash merge rewrites the commits, so the ancestry
# test above fails even though the work is merged and the branch is safe to
# delete.
#
# The question is "is there a MERGED pull request for this branch", not "what
# is the state of the first pull request". Those differ as soon as a branch has
# two: a failed attempt leaves a closed or open one behind, and taking the
# first would then report the branch as unmerged forever, refusing a cleanup
# that is entirely safe.
if [ "$merged" -eq 0 ] && command -v gh >/dev/null 2>&1; then
  states=$(gh pr list --head "$branch" --state all --json state --jq '.[].state' 2>/dev/null || true)

  case "$states" in
    *MERGED*)
      merged=1
      ;;
    *)
      if [ -n "$states" ]; then
        echo "error: no pull request for '$branch' has been merged" >&2
        echo "" >&2
        echo "  pull requests found for this branch: $states" >&2
        echo "" >&2
        echo "Cleaning up now would delete commits that exist nowhere else." >&2
        echo "Merge one of them first:" >&2
        echo "  gh pr list --head $branch --state all" >&2
        exit 1
      fi
      ;;
  esac
fi

if [ "$merged" -eq 0 ]; then
  echo "error: '$branch' is not merged into '$integration'" >&2
  echo "" >&2
  echo "There is no pull request for it, or the pull request is still open." >&2
  echo "If the work is genuinely finished elsewhere and this branch is" >&2
  echo "obsolete, delete it deliberately:" >&2
  echo "  git worktree remove --force \"$worktree\"" >&2
  echo "  git branch -D \"$branch\"" >&2
  echo "  git push origin --delete \"$branch\"" >&2
  exit 1
fi

# --- remove -----------------------------------------------------------------

if [ "$force" -eq 0 ] && [ -n "$(git -C "$worktree" status --porcelain 2>/dev/null)" ]; then
  echo "error: worktree for '$task' has uncommitted changes" >&2
  echo "" >&2
  git -C "$worktree" status --short >&2
  echo "" >&2
  echo "Commit them, stash them, or discard them, then re-run." >&2
  echo "To discard them and remove the worktree anyway: --force" >&2
  exit 1
fi

echo "Removing worktree $worktree"
if [ "$force" -eq 1 ]; then
  git worktree remove --force "$worktree"
else
  git worktree remove "$worktree"
fi

# Build output lives inside the worktree and goes with it, but a stray
# directory can survive if the worktree was removed by hand.
stray_build="$repo_root/build/$(basename "$worktree")"
if [ -d "$stray_build" ]; then
  echo "Removing stray build directory $stray_build"
  rm -rf "$stray_build"
fi

echo "Deleting local branch $branch"
git branch -D "$branch"

if git ls-remote --exit-code --heads origin "$branch" >/dev/null 2>&1; then
  echo "Deleting remote branch $branch"
  git push origin --delete "$branch"
else
  echo "Remote branch $branch does not exist; nothing to delete."
fi

# Worktrees leave administrative files behind when removed abruptly, and
# prune is the supported way to clear them.
git worktree prune

echo ""
echo "Cleaned up task '$task'."
