#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# wave-new <wave> <task>
#
# Start a task: verify its declaration, create its branch from the latest
# integration branch, add a worktree, and prepare its build directory.
#
# Refuses to start if another task in the same wave declares overlapping file
# territory. That check is the whole point of the harness: a conflict found
# now costs nothing, and the same conflict found after a day of parallel work
# costs the day.
#
# Usage:
#   scripts/harness/wave-new.sh 1 canvas
#   scripts/harness/wave-new.sh 0 core
# ---------------------------------------------------------------------------

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=lib/harness.sh
. "$script_dir/lib/harness.sh"

usage() {
  cat <<'USAGE'
usage: wave-new.sh <wave> <task>

  wave   wave number, for example 0 or 1
  task   task name, matching tasks/wave-<wave>/<task>.md

Creates branch wave/<wave>-<task> from the integration branch and a worktree
at .worktrees/<wave>-<task>. Refuses if the task's declared file territory
overlaps another task in the same wave.
USAGE
}

if [ "$#" -ne 2 ]; then
  usage >&2
  exit 2
fi

wave="$1"
task="$2"

if ! printf '%s' "$wave" | grep -qE '^[0-9]+$'; then
  echo "error: wave must be a number, got '$wave'" >&2
  exit 2
fi

repo_root=$(harness_repo_root)
cd "$repo_root"

harness_require_dev
harness_require_task_file "$wave" "$task"

slug=$(harness_slug "$wave" "$task")
branch=$(harness_branch "$wave" "$task")
worktree=$(harness_worktree_path "$wave" "$task")
build_rel=$(harness_build_dir "$wave" "$task")

# --- refusals before anything is created -----------------------------------

if git rev-parse --verify --quiet "refs/heads/$branch" >/dev/null; then
  echo "error: branch '$branch' already exists" >&2
  echo "Finish it with wave-done, or remove it with wave-clean." >&2
  exit 1
fi

if [ -d "$worktree" ]; then
  echo "error: worktree '$worktree' already exists" >&2
  echo "Remove it with wave-clean, or pick another task name." >&2
  exit 1
fi

# --- territory check -------------------------------------------------------

echo "Checking wave ${wave} territory..."
harness_check_wave_territories "$wave" || exit 1

# --- create ----------------------------------------------------------------

integration=$(harness_integration_branch)

# Branch from the remote integration branch when one exists, so a task never
# starts from a stale local copy. Falling back to the local branch keeps the
# harness usable before the first push.
base="$integration"
if git rev-parse --verify --quiet "refs/remotes/origin/$integration" >/dev/null; then
  git fetch origin "$integration" --quiet
  base="origin/$integration"
fi

echo "Creating $branch from $base"
git worktree add -b "$branch" "$worktree" "$base" --quiet

# The build directory lives inside the worktree so parallel builds never share
# an object file or a CMake cache. It is git-ignored by the "build/" rule.
mkdir -p "$worktree/$build_rel"

echo ""
echo "Ready."
echo ""
echo "  worktree   $worktree"
echo "  branch     $branch"
echo "  build dir  $build_rel"
echo ""
echo "  territory  (from tasks/wave-${wave}/${task}.md)"
while IFS= read -r line; do
  [ -n "$line" ] && echo "             $line"
done < <(harness_task_territory "$(harness_task_file "$wave" "$task")")
echo ""
echo "Next:"
echo "  cd \"$worktree\""
echo "  scripts/fetch-deps.ps1        # once per worktree"
echo ""
echo "Edit only inside the territory above. When the task is finished:"
echo "  scripts/harness/wave-done.sh $task"
