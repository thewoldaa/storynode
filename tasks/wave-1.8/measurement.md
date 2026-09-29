# Wave 1.8 — Interface measurement

A place for tools that measure the interface, and a way to run them.

## Why this wave exists

The `canvas` task has an acceptance criterion that cannot be checked by
looking: "a 500-node story pans at a steady frame rate, measured, with the
number recorded". Measuring it needs a tool, and the tool has nowhere to live.

It cannot go in `src/ui/assets/canvas/`. The build globs that tree and inlines
every `.js` it finds into the page, so a benchmark there ships inside the
application. The `canvas` task found this and was right about it.

It can go in `scripts/`, which is where the harness and the dependency fetcher
live. But a benchmark in `scripts/` is a script nobody runs: it is not wired
into CTest, so the claim it supports is never re-checked, and the next change
to the canvas can quietly make it false.

That is the actual problem. Not where the file goes — whether the number stays
true.

## Territory

- src/tools/**
- scripts/**
- cmake/**
- src/CMakeLists.txt
- CMakeLists.txt
- tests/**
- docs/**
- CHANGELOG.md

## Deliverables

1. **A home for interface measurement tools.** `src/tools/` already holds
   `probe_resources`. This wave adds the canvas benchmark there, alongside it,
   and the convention is written down: a tool that measures the interface is a
   C++ or Node program under `src/tools/`, never a file inside the asset tree.

2. **The benchmark, wired into CTest.** It runs as part of the suite, at a node
   count small enough to keep the suite fast, and reports the frame time it
   measured. A benchmark that is not in the suite is a benchmark that stops
   being true.

3. **A budget, and what happens when it is missed.** The test fails when the
   frame time exceeds a threshold, so a regression is caught rather than
   discovered later. The threshold is chosen from the measured headless
   baseline plus a margin, not guessed — see the note on baselines below.

4. **A larger, opt-in run.** The full 500-node measurement is too slow for
   every test run. It is a separate CTest test, excluded from the default
   suite and run on demand, and its number is what the canvas acceptance
   criterion refers to.

5. **The baseline problem, solved.** A frame-rate number measured in a headless
   browser is bounded by the headless browser's own pacing, not by the
   application. The `canvas` task noticed this — it refused to call 49.5 fps a
   real ceiling without evidence — and was right to. This wave measures the
   empty-page ceiling and reports both numbers, so a frame time can be read as
   a fraction of what the environment allows rather than as an absolute.

## Acceptance criteria

- [ ] `ctest` passes, including the new benchmark test.
- [ ] The benchmark reports the empty-page ceiling and the loaded-page frame
      time, both, so the second can be read against the first.
- [ ] Deliberately making the canvas slow fails the test. Verify this by doing
      it, not by reasoning about it.
- [ ] The full 500-node run is available as an opt-in test and its number is
      recorded in the pull request.
- [ ] No file under `src/ui/assets/**` is a measurement tool.

## Notes

The `canvas` task wrote `scripts/canvas-bench.cjs` and reported that it was
outside its territory rather than quietly keeping it. That is the behaviour the
territory rule is for: the file was not wrong, it was in the wrong place, and
the task that found it was not the task that should decide where it goes.

This wave also settles a question the split raised: `src/ui/assets/` is for
things that ship. A tool that reads them is not one of them.
