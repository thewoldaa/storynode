# UI engine

The interface is HTML, CSS and JavaScript rendered by WebView2, with C++
owning the document, the undo stack, file I/O and the export pipeline.

This mirrors how SpoutCam's settings panel is built, and the reasoning is
worth recording because the alternative — drawing the UI with Win32 controls —
looks simpler at the start and is not.

## Why HTML for the interface

A node graph editor is mostly a canvas: panning, zooming, hit-testing,
edge routing, drag interactions, text layout. Win32 gives none of that.
`Direct2D` gives primitives but no layout, no hit-testing, no text wrapping,
no scroll containers, and every control is hand-built. The result is several
thousand lines of rendering code before the first node appears.

HTML gives all of it, plus CSS transitions and a layout engine that already
handles the cases a hand-rolled one gets wrong. The interface is then a
document rather than a painting routine, which is the difference between
adding a property row by editing markup and adding it by writing layout code.

## How the pieces fit

```
┌─────────────────────────────────────────────────────────┐
│  Win32 window                                            │
│                                                          │
│  ┌───────────────────────┐  ┌────────────────────────┐  │
│  │  WebView2 control      │  │  native child window    │  │
│  │  (the entire UI)       │  │  (video, or nothing)    │  │
│  └───────────────────────┘  └────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

- **C++ owns the document.** The model, undo stack, validation and I/O live
  in `src/core/` and have no dependency on Win32 or WebView2, so they are
  unit-testable and reusable by the exporters.
- **JavaScript owns presentation.** Layout, painting and interaction live in
  `src/ui/`. The JS side holds no authoritative state; it renders what C++
  tells it and reports what the user did.
- **The bridge carries messages, not state.** C++ sends a complete snapshot
  when the document changes; JS sends intent (`{"type":"moveNode", ...}`).
  Neither side asks the other questions.

### Why a snapshot rather than a diff

A diff protocol is smaller on the wire and much harder to get right. The
document is a few thousand nodes at most, which serialises to well under a
megabyte, and a full snapshot is trivially correct: there is no possibility of
the two sides disagreeing about the current state. Optimise it later if a
profile says to.

## The bridge

Two directions, both already implemented in `src/app/`:

**JS to C++.** `window.chrome.webview.postMessage(json)` on the page side,
`add_WebMessageReceived` on the host side. The message is a JSON object with a
`type` field.

**C++ to JS.** `ICoreWebView2::ExecuteScript` calling a single global entry
point, `window.storynode.receive(json)`. One entry point rather than many
named callbacks, so adding a message type never changes the host code.

The handshake matters: the page posts `{"type":"ready"}` when its script has
finished loading, and C++ sends nothing before it arrives. Without that,
`ExecuteScript` calls made during navigation are silently dropped and the
interface appears to load with no data.

## The HTML lives in the binary

`ui.html` is embedded as an `RCDATA` resource and loaded with
`NavigateToString`. No temporary files, no local HTTP server, and nothing to
go missing at runtime.

For a page that grows past one file, the same trick works with a virtual host
mapping: `SetVirtualHostNameToFolderMapping` maps a hostname such as
`storynode.local` to a directory, and the page loads `storynode.local/app.js`
normally. That keeps the origin model intact — `file://` URLs have opaque
origins and cannot fetch siblings, which is a wall worth avoiding rather than
discovering later.

## What not to route through the bridge

Anything that updates every frame. SpoutCam draws its video preview into a
plain child window positioned over a slot in the page rather than pushing
frames through the bridge, because a frame per repaint costs more than the
rest of the interface put together.

The same rule applies here: if a surface needs 60 updates a second — a
timeline scrub, a live preview — it belongs in a native child window over the
page, or it should be batched. A node drag sends one message when it ends, not
one per mouse move.

## Runtime dependency

WebView2 Evergreen is preinstalled on Windows 11 and was pushed to eligible
Windows 10 devices, so a user is not asked to install anything. The app should
still check at startup and fail with a clear message rather than a blank
window if the runtime is somehow absent:

```
GetAvailableCoreWebView2BrowserVersionString(nullptr, &version)
```

The SDK is linked statically (`WebView2LoaderStatic.lib`), so there is no
`WebView2Loader.dll` to ship beside the executable.

## Alternatives considered

| Engine | Why not |
| --- | --- |
| Sciter | No CSS flexbox — uses `flow:horizontal` and `1*` units. Every stylesheet would need rewriting, and the free tier forbids static linking. |
| Ultralight | Paid above $100K revenue, and WebGL, WebRTC and video are missing or experimental. |
| RmlUi | Excellent for a game rendering to a texture, but markup is RML/RCSS and scripting is Lua, so it is not an HTML/JS interface. |
| Trident (`IWebBrowser2`) | IE11-era engine: no CSS custom properties, no arrow functions, no `let`/`const`. Microsoft is removing it from its own products. |
| Local server and a browser | No window integration, no native file dialogs, and the UI lives in a browser tab. |
