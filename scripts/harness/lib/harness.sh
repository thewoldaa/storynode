#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Shared functions for the wave harness.
#
# Sourced by every wave-*.sh script. Not executable on its own.
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

set -euo pipefail

# --- repository layout -----------------------------------------------------

harness_repo_root() {
  git rev-parse --show-toplevel
}

harness_worktree_root() {
  echo "$(harness_repo_root)/.worktrees"
}

harness_integration_branch() {
  echo "${STORYNODE_INTEGRATION_BRANCH:-dev}"
}

# --- naming ----------------------------------------------------------------
#
# A task is identified by its wave and name, and everything derived from it
# uses the same slug so that branch, worktree, build directory and task file
# can all be found from the slug alone.

harness_slug() {
  local wave="$1" task="$2"
  # Lowercase and replace anything that is not alphanumeric, dot, dash or
  # underscore. Slashes are deliberately excluded: a slash in a task name
  # would make the build directory nested and the branch ambiguous.
  printf '%s-%s' "$wave" "$task" \
    | tr '[:upper:]' '[:lower:]' \
    | sed 's/[^a-z0-9._-]/-/g'
}

harness_branch() {
  local wave="$1" task="$2"
  echo "wave/$(harness_slug "$wave" "$task")"
}

harness_worktree_path() {
  local wave="$1" task="$2"
  echo "$(harness_worktree_root)/$(harness_slug "$wave" "$task")"
}

harness_build_dir() {
  local wave="$1" task="$2"
  echo "build/$(harness_slug "$wave" "$task")"
}

harness_task_file() {
  local wave="$1" task="$2"
  echo "$(harness_repo_root)/tasks/wave-${wave}/${task}.md"
}

# Find the worktree for a task name alone, by scanning git's own worktree
# list. Used by wave-sync/done/clean, which take only a task name because
# requiring the wave too would mean remembering it.
#
# The branch is "wave/<wave>-<task>" and the worktree directory is the same
# slug, so the match is on the segment after the final dash: the task name
# alone is what the caller has. Matching "wave-<task>" instead would miss
# every real case, because the slug always begins with the wave number.
harness_worktree_for_task() {
  local task="$1"
  local path=""

  while IFS= read -r line; do
    case "$line" in
      worktree\ *) path="${line#worktree }" ;;
      branch\ *)
        local branch="${line#branch refs/heads/}"
        local slug="${branch#wave/}"
        # Compare the trailing "-<task>" segment exactly, so task "canvas"
        # does not match "canvas-layers" and "core" does not match "core-ui".
        case "$slug" in
          *-"$task")
            echo "$path"
            return 0
            ;;
        esac
        ;;
    esac
  done < <(git worktree list --porcelain)

  return 1
}

# --- task declaration ------------------------------------------------------

harness_require_task_file() {
  local wave="$1" task="$2"
  local file
  file=$(harness_task_file "$wave" "$task")

  if [ ! -f "$file" ]; then
    echo "error: no task declaration at tasks/wave-${wave}/${task}.md" >&2
    echo "" >&2
    echo "Create it first. It must contain a '## Territory' section listing" >&2
    echo "the paths this task owns, for example:" >&2
    echo "" >&2
    echo "    ## Territory" >&2
    echo "" >&2
    echo "    - src/ui/assets/canvas/**" >&2
    echo "    - src/ui/assets/styles/canvas.css" >&2
    echo "" >&2
    echo "See tasks/wave-0/core.md for a worked example." >&2
    return 1
  fi
}

# Print the territory entries of a task file, one per line.
#
# Reads the lines under "## Territory" until the next heading at the SAME
# level. A deeper heading — "### Why the core is in scope" — does not end the
# section, but its bullet list must not be mistaken for territory either.
#
# That distinction is not cosmetic. Without it, a bullet list in a sub-section
# explaining a decision is read as a set of file claims, which both corrupts
# the overlap check and silently widens what the task is allowed to touch. The
# rules for what counts as territory should not be loosenable by writing prose
# under them.
#
# So an entry is a bullet that looks like a path: no whitespace, and only the
# characters a path can contain. Note the asterisk in that set — leaving it out
# drops every wildcard claim, which makes the overlap check useless in the
# quietest possible way: two tasks could both claim src/core/** and neither
# would be seen.
harness_task_territory() {
  local file="$1"
  awk '
    /^##[[:space:]]+Territory[[:space:]]*$/ { in_section = 1; next }
    /^##[[:space:]]/ { in_section = 0 }
    /^###[[:space:]]/ { next }
    in_section && /^[[:space:]]*-[[:space:]]/ {
      sub(/^[[:space:]]*-[[:space:]]*/, "")
      sub(/[[:space:]]+#.*$/, "")
      gsub(/[[:space:]]+$/, "")
      # A path claim: no whitespace anywhere, and no prose punctuation.
      # "src/core/**" qualifies. "Two of them are in the file format."
      # does not, because of the spaces.
      if ($0 ~ /^[A-Za-z0-9._*\/-]+$/ && length($0) > 0) print
    }
  ' "$file"
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

harness_validate_territory_line() {
  local line="$1"
  if ! printf '%s' "$line" | grep -qE '^[A-Za-z0-9._/-]+(/\*\*|/\*)?$'; then
    return 1
  fi
  # Reject ".." segments: a territory must not escape the repository.
  if printf '%s' "$line" | grep -qE '(^|/)\.\.(/|$)'; then
    return 1
  fi
  return 0
}

# Reduce a territory line to its comparable directory prefix.
harness_territory_prefix() {
  local line="$1"
  printf '%s' "$line" | sed -E 's#/\*\*$##; s#/\*$##'
}

# Does a territory line mean "this directory and below"?
harness_territory_recursive() {
  local line="$1"
  case "$line" in
    */\*\*) return 0 ;;
    *) return 1 ;;
  esac
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
harness_territories_overlap() {
  local a="$1" b="$2"
  local pa pb
  pa=$(harness_territory_prefix "$a")
  pb=$(harness_territory_prefix "$b")

  # Exact same prefix always conflicts.
  if [ "$pa" = "$pb" ]; then
    return 0
  fi

  # One is inside the other. The deeper one must be reached through a
  # boundary, so append a separator to the shorter before comparing.
  local shorter longer
  if [ "${#pa}" -lt "${#pb}" ]; then shorter="$pa"; longer="$pb"; else shorter="$pb"; longer="$pa"; fi

  case "$longer" in
    "$shorter"/*)
      # The deeper path is inside the shallower one. This only conflicts if
      # the shallower entry is recursive, or if the deeper entry is exactly
      # one level down (which "/*" covers).
      if harness_territory_recursive "$a" || harness_territory_recursive "$b"; then
        return 0
      fi
      local rest="${longer#"$shorter"/}"
      if [ "${rest#*/}" = "$rest" ]; then
        # Exactly one segment below: the direct-children case.
        return 0
      fi
      return 1
      ;;
  esac

  return 1
}

# Check every pair of territories in a wave for overlap.
harness_check_wave_territories() {
  local wave="$1"
  local tasks_dir
  tasks_dir="$(harness_repo_root)/tasks/wave-${wave}"

  if [ ! -d "$tasks_dir" ]; then
    return 0
  fi

  local -a names=()
  local -a territories=()
  local f name line

  for f in "$tasks_dir"/*.md; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .md)
    while IFS= read -r line; do
      [ -z "$line" ] && continue
      names+=("$name")
      territories+=("$line")
    done < <(harness_task_territory "$f")
  done

  local i j
  for (( i=0; i<${#territories[@]}; i++ )); do
    for (( j=i+1; j<${#territories[@]}; j++ )); do
      # A task never conflicts with itself; the same task declaring a
      # directory and a file inside it is redundant but harmless.
      if [ "${names[$i]}" = "${names[$j]}" ]; then
        continue
      fi
      if harness_territories_overlap "${territories[$i]}" "${territories[$j]}"; then
        echo "error: territory conflict in wave ${wave}" >&2
        echo "" >&2
        echo "  tasks/wave-${wave}/${names[$i]}.md" >&2
        echo "    declares: ${territories[$i]}" >&2
        echo "  tasks/wave-${wave}/${names[$j]}.md" >&2
        echo "    declares: ${territories[$j]}" >&2
        echo "" >&2
        echo "Two tasks in one wave must never write the same files. Move the" >&2
        echo "shared surface into a core task in its own wave, or narrow one" >&2
        echo "of the two territories." >&2
        return 1
      fi
    done
  done

  return 0
}

# Does a repository-relative path fall inside a territory entry?
harness_path_in_territory() {
  local path="$1" territory="$2"
  local prefix
  prefix=$(harness_territory_prefix "$territory")

  if [ "$path" = "$prefix" ]; then
    return 0
  fi

  case "$path" in
    "$prefix"/*)
      if harness_territory_recursive "$territory"; then
        return 0
      fi
      local rest="${path#"$prefix"/}"
      if [ "${rest#*/}" = "$rest" ]; then
        return 0
      fi
      return 1
      ;;
  esac

  return 1
}

# Is this path always allowed, whatever a task declares?
#
# Three categories, and the test for all of them is the same: does an
# unnoticed overlap here produce silently wrong work, or a noisy conflict?
#
# `tests/` — every task that adds behaviour adds a test for it, and a task that
# adds behaviour without a test is the thing the tests exist to prevent. So the
# test tree is not a territory to be claimed; it is a consequence of doing the
# work. Requiring each declaration to list it produces a rule that is either
# restated in every file or broken by every file, and a rule that is always
# broken stops being read. It was widened after the fact twice before this was
# written down, which is the signal that the rule itself was wrong.
#
# `docs/` and `CHANGELOG.md` — every task documents what it did, and both are
# append-only in practice: a task adds a section, it does not rewrite one.
# Two tasks adding a section at the same place produce a textual conflict that
# git refuses to merge and a person resolves. Nothing is silently wrong.
#
# That is the distinction that matters, and it is not the same as "these files
# are not important". `src/ui/assets/**` and `src/app/**` stay claimed because
# an unnoticed overlap there produces code that merges cleanly and is broken:
# two tasks editing one stylesheet, or one window procedure, with no conflict
# to notice. A noisy conflict is safe; a silent one is not.
#
# A task's own declaration — see the note in harness_check_task_changes.
harness_path_always_allowed() {
  local path="$1"
  case "$path" in
    tests/*) return 0 ;;
    docs/*) return 0 ;;
    CHANGELOG.md) return 0 ;;
  esac
  return 1
}

# Check that every file a task has touched falls inside its territory.
harness_check_task_changes() {
  local wave="$1" task="$2" worktree="$3"
  local file
  file=$(harness_task_file "$wave" "$task")

  local -a territories=()
  local line
  while IFS= read -r line; do
    [ -n "$line" ] && territories+=("$line")
  done < <(harness_task_territory "$file")

  if [ "${#territories[@]}" -eq 0 ]; then
    echo "error: tasks/wave-${wave}/${task}.md declares no territory" >&2
    return 1
  fi

  local integration
  integration=$(harness_integration_branch)

  # Files this branch changed relative to where it forked from dev.
  local -a changed=()
  local f
  while IFS= read -r f; do
    [ -n "$f" ] && changed+=("$f")
  done < <(git -C "$worktree" diff --name-only "$integration"...HEAD 2>/dev/null || true)

  # Uncommitted work counts too: wave-done runs before the final commit in
  # some workflows, and a violation that is only visible after committing is
  # a violation found too late.
  while IFS= read -r f; do
    [ -n "$f" ] && changed+=("$f")
  done < <(git -C "$worktree" status --porcelain 2>/dev/null | awk '{ $1=""; sub(/^ /,""); print }')

  if [ "${#changed[@]}" -eq 0 ]; then
    return 0
  fi

  local -a outside=()
  for f in "${changed[@]}"; do
    # A task always owns its own declaration. Requiring a task to list its
    # declaration in its declaration is a circle, and the file is the task's
    # own description of itself.
    if [ "$f" = "tasks/wave-${wave}/${task}.md" ]; then
      continue
    fi

    # Paths that are a consequence of the work rather than a place to work.
    # See harness_path_always_allowed.
    if harness_path_always_allowed "$f"; then
      continue
    fi

    # Territory is checked against committed paths only. A file that exists
    # in the working tree but is git-ignored (a build product) is not the
    # task's responsibility.
    local inside=1
    for line in "${territories[@]}"; do
      if harness_path_in_territory "$f" "$line"; then inside=0; break; fi
    done
    if [ "$inside" -ne 0 ]; then
      outside+=("$f")
    fi
  done

  if [ "${#outside[@]}" -gt 0 ]; then
    echo "error: task ${wave}/${task} edited files outside its territory" >&2
    echo "" >&2
    echo "  declared territory:" >&2
    for line in "${territories[@]}"; do echo "    $line" >&2; done
    echo "" >&2
    echo "  files outside it:" >&2
    for f in "${outside[@]}"; do echo "    $f" >&2; done
    echo "" >&2
    echo "Either move the change into a task that owns those files, or widen" >&2
    echo "this task's territory in tasks/wave-${wave}/${task}.md and re-run." >&2
    return 1
  fi

  return 0
}

# --- tool lookup ------------------------------------------------------------
#
# Git Bash does not inherit a PATH entry added to Windows after the shell
# started, and CMake's installer adds itself to the Windows PATH. So a fresh
# Git Bash cannot see a cmake that was installed this morning, and the harness
# fails with "cmake: command not found" on a machine where cmake plainly
# works.
#
# Look in the standard install locations before giving up, rather than
# requiring every caller to have a correctly inherited PATH.

harness_find_tool() {
  local name="$1"
  shift

  if command -v "$name" >/dev/null 2>&1; then
    command -v "$name"
    return 0
  fi

  local candidate
  for candidate in "$@"; do
    if [ -x "$candidate" ]; then
      echo "$candidate"
      return 0
    fi
  done

  return 1
}

harness_cmake() {
  local found
  found=$(harness_find_tool cmake \
    "/c/Program Files/CMake/bin/cmake.exe" \
    "/c/Program Files (x86)/CMake/bin/cmake.exe" \
    "$HOME/AppData/Local/Programs/CMake/bin/cmake.exe" \
    "/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
    "/c/Program Files/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
    "/c/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe") || {
    echo "error: cmake not found" >&2
    echo "Install it, or add it to PATH:" >&2
    echo "    winget install Kitware.CMake" >&2
    return 1
  }
  echo "$found"
}

harness_ctest() {
  local found
  found=$(harness_find_tool ctest \
    "/c/Program Files/CMake/bin/ctest.exe" \
    "/c/Program Files (x86)/CMake/bin/ctest.exe" \
    "$HOME/AppData/Local/Programs/CMake/bin/ctest.exe") || {
    echo "error: ctest not found. It ships with CMake." >&2
    return 1
  }
  echo "$found"
}

# --- build and test --------------------------------------------------------

# Configure, build and test inside a worktree.
#
# The build directory is per-task so parallel builds never share an object
# file or a CMake cache. Everything is done through the worktree's own copy
# of the sources, so a build here cannot see another task's changes.
harness_build_and_test() {
  local worktree="$1" slug="$2"

  local cmake_bin ctest_bin
  cmake_bin=$(harness_cmake) || return 1
  ctest_bin=$(harness_ctest) || return 1

  local build_dir="$worktree/build/$slug"
  mkdir -p "$build_dir"

  echo "--- configure ($slug)"
  "$cmake_bin" -S "$worktree" -B "$build_dir" \
        -G "Visual Studio 17 2022" -A x64 \
        -DSTORYNODE_BUILD_TESTS=ON \
        > "$build_dir/configure.log" 2>&1 \
    || { echo "configure failed; see $build_dir/configure.log" >&2; tail -40 "$build_dir/configure.log" >&2; return 1; }

  echo "--- build ($slug)"
  "$cmake_bin" --build "$build_dir" --config Debug --parallel \
        > "$build_dir/build.log" 2>&1 \
    || { echo "build failed; see $build_dir/build.log" >&2; tail -60 "$build_dir/build.log" >&2; return 1; }

  echo "--- test ($slug)"
  "$ctest_bin" --test-dir "$build_dir" -C Debug --output-on-failure \
        > "$build_dir/test.log" 2>&1 \
    || { echo "tests failed; see $build_dir/test.log" >&2; tail -60 "$build_dir/test.log" >&2; return 1; }

  echo "--- ok"
}

# --- misc ------------------------------------------------------------------

harness_require_clean_tree() {
  local where="${1:-$(pwd)}"
  if [ -n "$(git -C "$where" status --porcelain 2>/dev/null)" ]; then
    echo "error: working tree at $where has uncommitted changes" >&2
    echo "Commit or stash them before running this command." >&2
    return 1
  fi
}

harness_require_dev() {
  if ! git rev-parse --verify --quiet "$(harness_integration_branch)" >/dev/null; then
    echo "error: integration branch '$(harness_integration_branch)' does not exist" >&2
    echo "" >&2
    echo "Create it from main first:" >&2
    echo "    git branch dev main" >&2
    return 1
  fi
}
