# StoryNode

A visual editor for branching interactive fiction. You lay out a story as a
graph of nodes, edit each node's properties, and export the result to JSON,
WebGAL script, or a self-contained HTML player.

The editor is a native Windows application whose entire interface is HTML,
CSS and JavaScript rendered by WebView2. The C++ side owns the document, the
undo stack, file I/O and the export pipeline; the JavaScript side owns layout
and painting. The two talk over a message bridge.

## Status

Early. The project is organised into waves, each wave a set of parallel tasks
that merge into `dev` before the next wave starts. See
[docs/ROADMAP.md](docs/ROADMAP.md) for the current wave and what it contains.

## Building

Requirements:

- Windows 10 1809 or later (WebView2 Runtime is preinstalled on Windows 11)
- Visual Studio 2022 with the Desktop development with C++ workload
- CMake 3.25 or later
- PowerShell 5.1 or later (7.x recommended)

```powershell
# Fetch the WebView2 SDK into third_party/ (one time)
powershell -ExecutionPolicy Bypass -File scripts/fetch-deps.ps1

# Configure and build
cmake --preset default
cmake --build --preset default

# Run
.\build\default\bin\storynode.exe
```

## Repository layout

```
src/
  core/        Document model, undo stack, project I/O, schema validation
  app/         Application shell, window, WebView2 host, message bridge
  platform/    Win32 file dialogs, DPI, window chrome
  ui/          HTML/CSS/JS interface (the entire visible UI)
    assets/    Stylesheets, scripts, icons
tools/         Python helpers (schema codegen, export validation)
tests/         Unit tests
scripts/
  harness/     Git worktree harness for parallel task execution
tasks/         Per-task declarations: file territory and acceptance criteria
docs/          Roadmap, architecture notes, development log
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The short version: one task, one
branch, one worktree, and never edit outside your declared file territory.

## License

MIT. See [LICENSE](LICENSE).
