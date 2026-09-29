# Changelog

Notable changes, newest first. Generated at the end of each wave from the
merge commits since the previous wave.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Repository scaffolding: license, contributing guide, ignore and attribute
  rules, and a checker that refuses to commit files which must not be public.
- Git worktree harness for running parallel tasks without interference:
  `wave-new`, `wave-list`, `wave-sync`, `wave-done`, `wave-clean`, in both
  PowerShell and Bash.
- Territory declarations under `tasks/`, with an overlap check that runs
  before a task starts.
- Roadmap, architecture notes, and a record of why the interface is built in
  HTML rather than with native controls.

[Unreleased]: https://github.com/thewoldaa/storynode/commits/dev
