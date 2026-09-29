#!/usr/bin/env bash
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
#   scripts/harness/wave-done.sh canvas
#   scripts/harness/wave-done.sh canvas --draft
# ---------------------------------------------------------------------------

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=lib/harness.sh
. "$script_dir/lib/harness.sh"

usage() {
  cat <<'USAGE'
usage: wave-done.sh <task> [--draft] [--no-build]

  --draft      open the pull request as a draft
  --no-build   skip configure/build/test (for docs-only tasks)

Runs the checks, pushes the task branch, and opens a PR into the integration
branch.
USAGE
}

draft=0
no_build=0
task=""

for arg in "$@"; do
  case "$arg" in
    --draft)    draft=1 ;;
    --no-build) no_build=1 ;;
    -h|--help)  usage; exit 0 ;;
    *)          task="$arg" ;;
  esac
done

if [ -z "$task" ]; then
  usage >&2
  exit 2
fi

repo_root=$(harness_repo_root)
cd "$repo_root"

harness_require_dev

worktree=$(harness_worktree_for_task "$task") || {
  echo "error: no worktree found for task '$task'" >&2
  echo "Run wave-list.sh to see the active tasks." >&2
  exit 1
}

branch=$(git -C "$worktree" rev-parse --abbrev-ref HEAD)
if [ "$branch" = "HEAD" ]; then
  echo "error: worktree for '$task' is in detached HEAD state" >&2
  exit 1
fi

integration=$(harness_integration_branch)

# The slug is the worktree directory name, which is also the build directory
# name inside it.
slug=$(basename "$worktree")
wave="${slug%%-*}"

echo "Finishing task $task ($branch)"
echo ""

# --- 1. territory ----------------------------------------------------------
#
# The declaration is required to exist and to be non-empty, and every file the
# branch touched must fall inside it. This runs before the build because a
# violation is a design problem, not a code problem, and no amount of passing
# tests makes it acceptable.

echo "--- territory"
if [ -f "$(harness_task_file "$wave" "$task")" ]; then
  harness_check_task_changes "$wave" "$task" "$worktree" || exit 1
  echo "ok"
else
  # The worktree name is wave-<wave>-<task>, so the wave is recoverable, but
  # the task file may live under a differently spelled wave directory. Say so
  # rather than guessing.
  echo "error: cannot find tasks/wave-${wave}/${task}.md" >&2
  echo "The worktree is named '${slug}'; if the wave or task name differs," >&2
  echo "run wave-done from the repository root with the task name that" >&2
  echo "matches its declaration file." >&2
  exit 1
fi

# --- 2. forbidden files ----------------------------------------------------
#
# Checked against the full branch diff, not just the last commit, because a
# forbidden file added in an earlier commit and deleted in a later one is
# still in the history that the pull request would merge.

echo "--- forbidden files"
committed=$(git -C "$worktree" diff --name-only "$integration"...HEAD || true)
staged=$(git -C "$worktree" diff --cached --name-only --diff-filter=ACMR || true)
unstaged=$(git -C "$worktree" diff --name-only --diff-filter=ACMR || true)
printf '%s\n%s\n%s\n' "$committed" "$staged" "$unstaged" \
  | sed '/^$/d' | sort -u \
  | bash "$repo_root/scripts/check-forbidden.sh" || exit 1
echo "ok"

# --- 3. build and test -----------------------------------------------------

if [ "$no_build" -eq 1 ]; then
  echo "--- build skipped (--no-build)"
else
  echo "--- build and test"
  harness_build_and_test "$worktree" "$slug" || exit 1
fi

# --- 4. push ---------------------------------------------------------------
#
# Everything is committed at this point or the build would have been
# meaningless, so refuse a dirty tree rather than pushing a branch whose
# contents differ from what was just tested.

echo "--- push"
if [ -n "$(git -C "$worktree" status --porcelain)" ]; then
  echo "error: worktree has uncommitted changes" >&2
  echo "Commit them first; the build just ran against the working tree and a" >&2
  echo "pushed branch must match what was tested." >&2
  exit 1
fi

if git ls-remote --exit-code --heads origin "$branch" >/dev/null 2>&1; then
  git -C "$worktree" push --force-with-lease origin "$branch"
else
  git -C "$worktree" push --set-upstream origin "$branch"
fi

# --- 5. pull request -------------------------------------------------------

echo "--- pull request"

existing=$(gh pr list --head "$branch" --base "$integration" --json number --jq '.[0].number' 2>/dev/null || true)
if [ -n "$existing" ]; then
  echo "Pull request #$existing already exists for $branch"
  gh pr view "$existing" --web
  exit 0
fi

# The body is generated from the task declaration so the PR states what the
# task promised, not just what changed. A reviewer comparing the two is the
# point of having declared acceptance criteria in the first place.
task_file="$(harness_task_file "$wave" "$task")"
title=$(head -1 "$task_file" | sed 's/^#[[:space:]]*//')
[ -z "$title" ] && title="wave ${wave}: ${task}"

body_file=$(mktemp)
trap 'rm -f "$body_file"' EXIT

{
  echo "Task: \`tasks/wave-${wave}/${task}.md\`"
  echo ""
  echo "## Declared territory"
  echo ""
  while IFS= read -r line; do
    [ -n "$line" ] && echo "- \`$line\`"
  done < <(harness_task_territory "$task_file")
  echo ""
  echo "## Declaration"
  echo ""
  # Everything below the first heading, so the PR carries the task's own
  # description, acceptance criteria and test plan.
  tail -n +2 "$task_file"
} > "$body_file"

pr_args=(--base "$integration" --head "$branch" --title "$title" --body-file "$body_file")
[ "$draft" -eq 1 ] && pr_args+=(--draft)

gh pr create "${pr_args[@]}"
