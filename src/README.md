# Source layout

```
core/        Document model, validation, undo, I/O, exporters.
             No Win32 headers, no WebView2. Builds and runs in a test process
             with no window and no message loop.

app/         Window, WebView2 host, message bridge dispatch, application
             lifecycle. The only layer that knows about both core and ui.

platform/    Win32 file dialogs, DPI handling, window chrome, shell
             integration. Isolated so core never needs a Windows header.

ui/          The interface: HTML, CSS and JavaScript, loaded into the web
             view as text. Depends on nothing.
  assets/    Stylesheets, scripts, icons.
```

## The dependency rule

```
ui  ←(messages only)←  app  →  core
                        ↓
                    platform
```

`core` is the floor: it depends on neither of the others. `app` depends on all
three. `ui` depends on nothing and communicates only through messages.

This is enforced by review, not by the build, so it is worth stating why it
matters: the moment `core` includes `<windows.h>`, the document model can no
longer be tested without a window, and every task that touches the model needs
the same window plumbing. The four parallel tasks in wave 1 are only
independent because the model they share has no such dependency.

## Adding files

Sources are globbed with `CONFIGURE_DEPENDS`, so a new `.cpp` file inside your
task's territory is picked up without editing a shared file. That is
deliberate: an explicit source list would be a file every task must edit, and
every task editing the same file is the conflict the harness exists to
prevent.
