#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# wave-list
#
# Every worktree, its branch, whether it has uncommitted changes, and how far
# it has drifted from the integration branch.
#
# Read-only. Safe to run at any time, including while other tasks are
# building.
#
# Usage:
#   scripts/harness/wave-list.sh
# ---------------------------------------------------------------------------

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=lib/harness.sh
. "$script_dir/lib/harness.sh"

repo_root=$(harness_repo_root)
cd "$repo_root"

integration=$(harness_integration_branch)

# Fetch quietly so ahead/behind counts against the real remote. A missing
# remote is not an error: the harness works before the first push.
if git remote get-url origin >/dev/null 2>&1; then
  git fetch origin --quiet 2>/dev/null || true
fi

if ! git rev-parse --verify --quiet "$integration" >/dev/null; then
  echo "error: integration branch '$integration' does not exist" >&2
  echo "Create it with: git branch $integration main" >&2
  exit 1
fi

# Column widths chosen from the longest realistic values rather than computed,
# so the output does not reflow between runs and is easy to read repeatedly.
printf '%-28s %-28s %-10s %-9s %s\n' "WORKTREE" "BRANCH" "STATE" "AHEAD" "DIRTY"
printf '%-28s %-28s %-10s %-9s %s\n' "----------------------------" "----------------------------" "----------" "---------" "-----"

path=""
branch=""
detached=0

print_row() {
  local p="$1" b="$2" d="$3"

  if [ ! -d "$p" ]; then
    printf '%-28s %-28s %-10s %-9s %s\n' "$(basename "$p")" "$b" "missing" "-" "-"
    return
  fi

  local state ahead behind dirty
  if [ "$d" -eq 1 ]; then
    state="detached"
  else
    state="ok"
  fi

  if [ "$d" -eq 0 ] && git rev-parse --verify --quiet "$b" >/dev/null 2>&1; then
    local counts
    counts=$(git rev-list --left-right --count "$integration...$b" 2>/dev/null || echo "0	0")
    behind=$(printf '%s' "$counts" | cut -f1)
    ahead=$(printf '%s' "$counts" | cut -f2)
    # Behind is worth flagging: a task that is behind needs a sync before its
    # PR is mergeable, and finding that out at push time is late.
    if [ "$behind" != "0" ]; then
      ahead="$ahead (behind $behind)"
    fi
  else
    ahead="-"
  fi

  if [ -n "$(git -C "$p" status --porcelain 2>/dev/null)" ]; then
    dirty="yes"
  else
    dirty="no"
  fi

  printf '%-28s %-28s %-10s %-9s %s\n' "$(basename "$p")" "$b" "$state" "$ahead" "$dirty"
}

while IFS= read -r line; do
  case "$line" in
    worktree\ *)
      # Print the previous entry when the next one starts.
      if [ -n "$path" ]; then
        print_row "$path" "$branch" "$detached"
      fi
      path="${line#worktree }"
      branch=""
      detached=0
      ;;
    branch\ refs/heads/*)
      branch="${line#branch refs/heads/}"
      ;;
    detached)
      detached=1
      branch="(detached)"
      ;;
    "")
      if [ -n "$path" ]; then
        print_row "$path" "$branch" "$detached"
        path=""
      fi
      ;;
  esac
done < <(git worktree list --porcelain; echo "")

echo ""
echo "Integration branch: $integration"
echo ""
echo "STATE 'detached' means the worktree is not on a branch and its commits"
echo "are reachable only from the reflog. That is almost always a mistake."
echo ""
echo "AHEAD shows commits on the task branch that $integration does not have."
echo "'(behind N)' means $integration has moved on; run wave-sync <task>."
