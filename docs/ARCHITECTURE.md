# Architecture

## Layers

```
src/ui/            HTML, CSS, JavaScript. Presentation only.
      ↑ ↓          message bridge
src/app/           Window, WebView2 host, bridge dispatch, application lifecycle.
      ↑ ↓
src/core/          Document model, validation, undo, I/O, exporters. No Win32.
src/platform/      File dialogs, DPI, registry, shell integration. Win32 only.
```

The dependency rule that keeps this testable: **`src/core/` must not include a
Windows header, and must not reference WebView2.** Everything in it builds and
runs in a test process with no window, no message loop and no display. If a
change to the core needs to know about a window, the change belongs in `app/`.

`src/ui/` depends on nothing at all. It is loaded as text into a web view and
talks to the rest of the program only through messages.

## The document model

```
Story
 ├─ id, title, metadata
 ├─ nodes: Node[]
 │    ├─ id, type, position {x, y}, size
 │    ├─ ports: Port[]        inputs and outputs, typed
 │    └─ data: key/value      type-specific properties, schema-checked
 └─ edges: Edge[]
      ├─ from: { nodeId, portId }
      └─ to:   { nodeId, portId }
```

Plain structs with no behaviour beyond validation and lookup. Behaviour lives
in the operations that act on them, which is what makes the undo stack
possible: an operation is a value that can be applied and reverted, and a
model that mutates itself in place cannot be undone without a snapshot.

Node types are data, not classes. A node type declares its ports and its
property schema, and the inspector renders the properties from that schema.
Adding a node type is adding a declaration, not a class — which is what keeps
wave 2 parallelisable.

## The bridge protocol

Every message is a JSON object with a `type` field.

**Page to host:**

| Type | Payload | Meaning |
| --- | --- | --- |
| `ready` | — | Page script loaded; host may now send state |
| `moveNode` | `id`, `x`, `y` | User finished dragging a node |
| `selectNode` | `id` or `null` | Selection changed |
| `setProperty` | `id`, `key`, `value` | Property edited in the inspector |
| `connect` | `from`, `to` | User drew an edge |
| `disconnect` | `edgeId` | User removed an edge |

**Host to page:**

| Type | Payload | Meaning |
| --- | --- | --- |
| `document` | full story | Authoritative state after any change |
| `selection` | `id` or `null` | Selection changed by the host |
| `validation` | `problems[]` | Current validation results |
| `error` | `message` | Something failed; show it |

Messages carry intent, not state. The host applies the intent to the
document, then broadcasts a fresh snapshot. The page never patches its own
copy optimistically, so the two sides cannot drift.

## Why the model is snapshotted rather than diffed

The document is small — a few thousand nodes serialises to well under a
megabyte — and a full snapshot is trivially correct. A diff protocol is
smaller on the wire and much harder to get right. If a profile later shows
snapshot cost mattering, the message boundary is already in the right place to
change it.

## Validation

Two kinds, deliberately separated:

- **Schema validation** runs on load and on every property edit. It reports
  every problem in the document rather than the first, because a user fixing
  one error to discover the next is a bad loop.
- **Graph validation** runs on demand and before export: unreachable nodes,
  dead ends, choices that lead nowhere, missing assets. These are warnings,
  not errors — an unfinished story is a valid document, just not a valid
  export.

Both produce the same `Problem` shape so the UI has one way to display them,
and the bridge sends both kinds in one message. Two channels would mean two
places to look for the same question.

## Unknown keys survive a round trip

Every level of the document — the story, each node, each port, each edge —
carries an `extra` object holding the keys that level's reader did not
recognise. Saving writes them back.

Without this, opening a file written by a newer build and saving it silently
deletes whatever that build added. That is the worst failure this format can
have: it destroys data without telling anyone, and it only shows up when the
user goes back to the newer version and finds their work gone.

The cost is that each reader has a list of the keys it knows about, and that
list has to be edited alongside the reader. A key added to the reader but
forgotten in the list would be written twice, once from the struct and once
from `extra` — which is why the parser rejects duplicate keys rather than
silently taking the last one.

## The interface is split, and why

```
src/ui/assets/
  ui.html              the shell: markup, message protocol, document state,
                       and the render loop
  canvas/              the graph area, its stylesheet and its script
  inspector/           the property panel, its stylesheet and its script
  styles/theme.css     the tokens and the chrome the shell owns
```

Each area registers itself with the shell and is called back to render. The
shell never calls an area by name, and an area never reads another area's
state. That is what makes the two independent enough to be written by two
tasks at once.

This is not tidiness. Wave 1 runs four tasks in parallel, two of which work on
the interface, and a single file holding both would make those two edit the
same file — the conflict the worktree harness exists to prevent, and one its
territory check refuses to start.

### How the assets get into the page

The page cannot load its own assets. It is delivered with `NavigateToString`,
which gives it an opaque origin, so a `<script src>` or a `fetch` to a sibling
is refused by the browser engine and there is no local server to ask instead.

So the build embeds every asset as a resource and the host inlines them into
the shell before navigating. The asset list is generated from a glob, not
written by hand: a hand-written list would be a shared file every task must
edit, which is the same problem one level down.

Adding a `.js` file under `canvas/` therefore makes it load, with no edit to
any file outside `canvas/`.

Two properties of the generated resource script cost real time to find, and
are recorded in `cmake/GenerateUiResources.cmake`:

- `rc.exe` does not treat quotes as delimiters around a resource name; it
  stores them as part of the name. A name written as `"UI.HTML"` is nine
  characters long, and a lookup for the seven-character name finds nothing.
- CMake treats a backslash in a string as an escape introducer, so a Windows
  path written into a generated file silently loses characters.

Both produce the same symptom: the asset is in the binary, cannot be found,
and the application shows a blank window with no error anywhere.
`probe_resources` exists to make that visible.

## What ships, and what measures it

`src/ui/assets/` is for things that ship. The build globs it and inlines every
`.js` and `.css` it finds into the page, so a file added there is in the
application whether it was meant to be or not.

A tool that measures the interface is therefore not one of those files. It
lives under `src/tools/`, alongside `probe_resources`, and is wired into CTest
so the number it produces stays true. A benchmark nobody runs is a benchmark
that quietly stops being correct.

That distinction came out of the `canvas` task, which needed to measure frame
time and found it had nowhere to put the tool.

## Verification

The interface is verified by having the page measure itself and report back
through the same bridge the editor uses. `storynode --verify` loads the page,
runs a script that reports the geometry of each band of the interface, and
exits non-zero if a band is missing, collapsed, or overlapping its neighbour.

This exists because a WebView2 host that fails to navigate, fails to create
the controller, or loads a page whose script throws will still show a window
and still stay running. Without a check, CI passes on a blank white rectangle.

The measurements come from `getBoundingClientRect` in the page's own
coordinate space, so the check is independent of window size, display scaling
and any screenshot mechanism. It runs at four window sizes, because a layout
that happens to be correct at one size is not evidence that it is correct: the
footer sitting at the bottom of a large window proves nothing about a small
one.

## Build

CMake with source globbing and `CONFIGURE_DEPENDS`. Globbing rather than an
explicit list is deliberate: with one worktree per task, an explicit source
list is a shared file every task must edit, which makes every task conflict
with every other task. Adding a source file inside a task's own territory must
not require touching a shared file.

WebView2 is optional. When the SDK is absent the core library, the bridge and
the tests still configure and build, so a contributor without it can work on
the model and the format — and the model tests run on a Linux CI runner, where
feedback is faster and the runner is cheaper.
