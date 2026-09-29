#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Reject commits and PRs that contain files which must never be public.
#
# Reads git path names from stdin, one per line, in the format produced by
#   git diff --cached --name-only --diff-filter=ACMR
# and exits non-zero if any path matches a forbidden pattern.
#
# This script is called from three places, and all three matter:
#   - scripts/install-hooks.sh wires it into .git/hooks/pre-commit
#   - .github/workflows/forbidden-files.yml runs it on every pull request
#   - wave-done.sh runs it before pushing
#
# The hook is the fast feedback; CI is the gate that actually holds, because a
# hook can be skipped with --no-verify and CI cannot. Do not weaken this to
# "warn only" on the grounds that the hook already ran.
#
# Usage:
#   bash scripts/check-forbidden.sh < paths.txt
#   git diff --cached --name-only --diff-filter=ACMR | bash scripts/check-forbidden.sh
#   bash scripts/check-forbidden.sh path/to/file another/path
# ---------------------------------------------------------------------------

set -u

# --- forbidden patterns ----------------------------------------------------
#
# Each entry is a POSIX extended regex matched against the whole path with
# grep -E. Paths use forward slashes on every platform because that is what
# git stores, so the patterns are written for that form and must not assume a
# Windows separator.
#
# Anchoring matters: "^CLAUDE\.md$" must not fire on "docs/CLAUDE.md.txt",
# and "^\.claude/" must not fire on "src/.claude-thing". Every pattern below
# is anchored at the start of a path segment or at the start of the line.

PATTERNS=(
  # Agent instruction files. These are working notes for whoever is editing
  # the code, not project documentation, and they routinely contain paths,
  # hostnames and reasoning that do not belong in a public repository.
  '(^|/)AGENTS\.md$'
  '(^|/)CLAUDE\.md$'
  '(^|/)\.claude/'
  '(^|/)\.agents/'
  '(^|/)\.cursor/'

  # Local scratch space. Worktrees are checkouts of this same repository and
  # committing one would duplicate the entire tree.
  '(^|/)\.worktrees/'
  '(^|/)agent-notes/'
  '(^|/)scratch/'
  '(^|/)personal/'

  # Credentials. Nothing in this project needs one to build or run, so any
  # match is a mistake and not a deliberate addition. The exception below
  # covers the files that legitimately carry these names as content rather
  # than as secrets.
  '(^|/)\.env$'
  '(^|/)\.env\..+'
  '(^|/)[^/]+\.local$'
  '(^|/)[^/]+\.key$'
  '(^|/)[^/]+\.pem$'
  '(^|/)secrets/'

  # Build output. Large, machine-specific, and regenerated on every machine.
  #
  # These are anchored to the repository root, unlike the categories above.
  # A nested "build/" is a legitimate directory name inside a source tree
  # (src/app/build/ is a reasonable place for build-system glue), and
  # rejecting it would be a false positive that blocks real work. The build
  # directories this rule exists to catch are always at the root: build/<task>
  # for the harness, build/ci for CI.
  '^build/'
  '^out/'
  '^\.vs/'
  '^\.idea/'
  '(^|/)CMakeUserPresets\.json$'
)

# --- explicit exceptions ---------------------------------------------------
#
# A path listed here is allowed even if a pattern above matches it. Keep this
# list short and justify every entry; it is the one place where the rule can
# be loosened, so an unexplained entry is a bug.

ALLOW=(
  # The checker itself contains the forbidden strings as patterns. It has to
  # be committed for CI to run it.
  '^scripts/check-forbidden\.sh$'
  '^scripts/check-forbidden\.ps1$'
  # The ignore file and this project's own documentation necessarily name the
  # forbidden paths.
  '^\.gitignore$'
  '^CONTRIBUTING\.md$'
  '^docs/'
  '^tasks/'
)

# --- read the path list ----------------------------------------------------
#
# Accept paths either as arguments or on stdin. The hook uses stdin; a manual
# invocation usually passes arguments.

if [ "$#" -gt 0 ]; then
  PATHS=$(printf '%s\n' "$@")
else
  PATHS=$(cat)
fi

if [ -z "$PATHS" ]; then
  exit 0
fi

is_allowed() {
  local path="$1"
  local pattern
  for pattern in "${ALLOW[@]}"; do
    if printf '%s' "$path" | grep -qE "$pattern"; then
      return 0
    fi
  done
  return 1
}

# --- check -----------------------------------------------------------------

VIOLATIONS=""
COUNT=0

while IFS= read -r path; do
  # git quotes paths containing unusual characters. Strip the surrounding
  # quotes so the patterns see the real name.
  path="${path%\"}"
  path="${path#\"}"

  [ -z "$path" ] && continue

  if is_allowed "$path"; then
    continue
  fi

  for pattern in "${PATTERNS[@]}"; do
    if printf '%s' "$path" | grep -qE "$pattern"; then
      VIOLATIONS="${VIOLATIONS}${path}"$'\n'
      COUNT=$((COUNT + 1))
      break
    fi
  done
done <<< "$PATHS"

# --- report ----------------------------------------------------------------

if [ "$COUNT" -gt 0 ]; then
  echo ""
  echo "=============================================================="
  echo " FORBIDDEN FILES DETECTED - commit refused"
  echo "=============================================================="
  echo ""
  echo "These paths must never enter this repository:"
  echo ""
  printf '%s' "$VIOLATIONS" | sed 's/^/   /'
  echo ""
  echo "Fix it with one of:"
  echo "   git restore --staged <path>        keep the file, unstage it"
  echo "   git rm --cached <path>             keep the file, untrack it"
  echo "   rm <path>                          delete it"
  echo ""
  echo "If the file is already committed, the history needs rewriting,"
  echo "not just a new commit that deletes it."
  echo "=============================================================="
  echo ""
  exit 1
fi

exit 0
