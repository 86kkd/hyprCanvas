# HyprCanvas user guide

[Back to the project](../README.md) · [Installation](../README.md#get-started)

This guide describes the Hyprland 0.57 Lua API. The plugin is named
`hyprCanvas`; its layout, options, and Lua namespace use `scroller`.
Old `scroller:movefocus`-style dispatcher bindings are not supported by this
version. Use `hl.plugin.scroller.move_focus(...)` and the other functions below.

## Canvas workspaces

A canvas workspace groups one native Hyprland workspace per connected monitor.
Switching canvases changes those member workspaces together. Special scratchpads
remain separate overlays.

```lua
hl.bind("SUPER + TAB", function()
    hl.plugin.scroller.switch_workspace("r")
end)
hl.bind("SUPER + SHIFT + TAB", function()
    hl.plugin.scroller.switch_workspace("l")
end)
hl.bind("SUPER + 1", function()
    hl.plugin.scroller.switch_workspace("1")
end)
```

Canvas switches slide according to the source and destination positions in the
canvas grid. A canvas to the right enters from the right while the current one
exits left; vertical and diagonal jumps follow their corresponding directions.
All monitors share that direction, with travel scaled to each monitor's logical
size. Numbered jumps use the same spatial rule rather than creation order.
Native workspace animation timing and the animations-enabled setting still apply.
Selecting the current canvas or cancelling an unchanged overview does not start
a canvas slide.

Numbers `"1"` through `"10"` address canvas creation order, **not native
Hyprland workspace IDs**. Only the next missing canvas can be created; jumping
past it does not allocate a chain of empty pages. A new canvas has separate
backing workspaces for each monitor and stays temporary until a window is opened
or moved into any member (including floating windows). An empty temporary canvas
cannot create another page beyond it. Leaving it removes it; once populated, it
remains a regular canvas even if its last window later closes.

During overview, switching changes the preview;
accepting the selection switches the actual desktop. Outside overview, it
switches immediately.

For a fuller set of bindings, copy
[`examples/canvas-layout.lua`](../examples/canvas-layout.lua) into
`~/.config/hypr/lua/canvas-layout.lua`, then load it after your normal
keybinding modules:

```lua
dofile(os.getenv("HOME") .. "/.config/hypr/lua/canvas-layout.lua")
```

The module installs these bindings in scroller. Both direction groups use
left / down / up / right order:

| Keys | Action |
| --- | --- |
| `Super + H / J / K / L` | Focus a window. |
| `Super + Alt + H / J / K / L` | Move a window. |
| `Ctrl + Shift + H / J / K / L` | Swap windows. |
| `Super + Ctrl + H / J / K / L` | Switch the canvas workspace in four directions. |
| `Super + N / M / comma / period` | Focus a physical monitor. |
| `Super + Alt + N / M / comma / period` | Move a window directly to an adjacent monitor and follow it. |
| `Super + I` | Open overview or accept its selection and exit. |
| `Super + F` | Expand or restore the active stack. |

Keep the left little finger on Super when navigating; add Alt with the ring
finger to move the selected window, including across monitors.

Read the module before installing it: its native fallbacks match the local
user configuration. Outside scroller it restores Super+Alt+H/J/K/L swapping,
Super+Ctrl+H/J/K/L movement, Ctrl+Shift+H/L workspace transfers, Super+N night
light, Super+M split ratio, and Super+comma/period workspace navigation.
Plugin-only shortcuts are removed. The old Super+Shift+N/M monitor movement
aliases are retired; those keys retain the notification panel and music launcher.

It also replaces Tab, number, and wheel workspace shortcuts, and disables
numbered window transfers and whole-workspace monitor moves while using scroller.
Other layouts retain native behavior. Number keys select workspaces; configure
visible-stack counts in Lua rather than binding the README's sizing examples.

Native workspace commands, gestures, and external workspace widgets still use
Hyprland's per-monitor semantics. Route ordinary canvas switching through
`switch_workspace` to keep all monitors together.

[`ChangeLayout.sh`](../examples/ChangeLayout.sh) switches between `master`,
`dwindle`, and `scroller` and installs the associated bindings. Its Lua path
also reloads the workspace module above, so install both files when using it.

## Canvas map

A compact map appears in the top-right workarea of the focused monitor for
1.5 seconds after directly switching canvases from the desktop, including empty
temporary canvases. Expiration does not depend on opening a window and leaves
no persistent badge or explanatory message. During overview
it stays on the monitor where overview opened and disappears immediately on
accept or cancel; exiting overview does not start a desktop HUD. Each cell represents a whole multi-monitor canvas;
positions preserve the actual canvas grid, including gaps and negative coordinates.

- A filled cell with a dot marks the current desktop; a gold outline marks the preview.
- Numbers follow canvas creation order, matching numbered canvas shortcuts.
- Dashed cells with `*` mark temporary or uncommitted canvases; dashed `+` cells
  advertise an adjacent creation slot. Empty temporary pages cannot extend further.
- During overview, click a cell to preview it or `+` to preview a new canvas.
  Accept overview to switch, or cancel to return. Desktop HUD clicks pass through.
- Large maps show a local region up to five rows/columns around the preview.
  Footer arrows indicate retained canvases beyond the visible region.

Set `plugin:scroller:canvas_map` to `0` to disable both map surfaces (default `1`).
The first version uses fixed placement and duration; the independent, zoomable
world-map design is tracked in [planned work](planned-work.md).

## Lanes and stacks

| Mode | Lane direction | Windows within a stack |
| --- | --- | --- |
| `row` | Horizontal | Vertical |
| `col` | Vertical | Horizontal |

Use `set_mode("row")` or `set_mode("col")` to change orientation.

### Sizing

`set_window_size(n)` sets how many stacks fit along the monitor's lane axis:
width in row mode, height in column mode. The count ranges from 1 to 8 and
applies across workspaces to existing and future stacks for the current session.
A typical monitor may use 2; an ultrawide may use 3 or 4.

A lane with a single window fills the workarea. Expanded or maximized stacks
keep their current span and return to the chosen size when restored. Free
resizing can override a stack's size until `set_window_size` is called again.
The `column_default_width` option supplies the initial setting before the
first call.

### Card stacks

Stacks with three or more windows automatically become overlapping cards:

- The active window gets 61.8% of the stack axis, the previously focused window
  gets 19.1%, and the remaining windows share 19.1%.
- The active card uses the full cross-axis span; the secondary uses 95%.
  Other cards shrink by a factor of 0.9 per layer outward on each side,
  starting from the secondary size. Cards stay centered.
- Window order is preserved. Without focus history, an adjacent window becomes
  the secondary card. Returning to one or two windows restores normal sizing.
- Background cards have edge labels. Pointer focus follows visible card areas
  and native animations rather than covered portions of windows.

Card movement uses Hyprland's existing animation settings. The plugin does not
register replacement animation curves.

### Stack expansion

`toggle_fullscreen()` expands the stack inside the layout model. It is separate
from Hyprland-native fullscreen and maximization, so scrolling and plugin focus
navigation remain available.

With multiple windows, the active window receives 90% of the stack axis.
For two windows, the other gets 10%; for three or more, the secondary gets 5%
and the remaining windows share 5%. Cross-axis card scaling still applies.
Switching focus transfers the large region to the newly focused window;
restoring the stack returns to its ordinary proportions.

## Moving windows

Use plugin focus and movement functions to reach windows outside the monitor's
visible area.

`move_window(direction)` has two behaviors:

| Direction | Behavior |
| --- | --- |
| Within a stack: up/down in row mode, left/right in column mode | Swap with the adjacent window. At the edge, route out of the stack without wrapping. |
| Along the lane: left/right in row mode, up/down in column mode | A single-window stack joins its neighbor; a window in a multi-window stack splits into a new stack on that side. |

Moving toward an expanded stack creates a separate stack instead of joining
it. Directional movement can cross physical monitors within the visible canvas.

`move_window_to_monitor(direction)` moves the active tiled window directly to
the adjacent physical monitor's visible workspace, even when there are local
windows in that direction. Focus follows the transferred window. It accepts
only cardinal directions and does nothing if there is no adjacent monitor or
overview is open.

```lua
hl.bind("SUPER + SHIFT + N", function() hl.plugin.scroller.move_window_to_monitor("l") end)
hl.bind("SUPER + SHIFT + period", function() hl.plugin.scroller.move_window_to_monitor("r") end)
```

`swap_window(direction)` exchanges the active windows of adjacent stacks in
the same lane. Focus follows the original window; stack sizes, membership
counts, and expansion states are preserved. It does not wrap, accept
`begin`/`end`, or act within the same stack. It is inactive during overview or
when either selected window is in native fullscreen.

```lua
hl.bind("SUPER + ALT + LEFT", function() hl.plugin.scroller.swap_window("l") end)
hl.bind("SUPER + ALT + RIGHT", function() hl.plugin.scroller.swap_window("r") end)
```

## Overview and marks

Overview is a navigation layer. It renders workspace cards while leaving real
window geometry untouched. It can select tiled windows, empty workspaces, and
new empty-workspace targets reached through directional navigation.

Directional focus moves between whole stacks, skipping sibling windows inside a
stack. The selection outline surrounds the entire stack, including all its
cards. Entering a stack selects its remembered active window. Empty workspaces
remain selectable.

While overview is open:

- `move_focus(direction)` changes the logical selection.
- `focus_monitor(direction)` changes the canvas preview selection.
- `toggle_overview("accept")` accepts the target.
- `cancel_overview()` restores the original workspace/window.

Calling `toggle_overview()` without an argument opens overview or accepts the
current selection if it is already open.

Marks are named references to windows across workspaces, including special
workspaces. Use `marks_add("name")` to record a window, `marks_visit("name")`
to focus it, `marks_delete("name")` to remove one mark, and `marks_reset()` to
clear all marks. Visiting a mark may switch workspaces.

## Lua API

All functions live under `hl.plugin.scroller`. Call them from keybinding
callbacks after the plugin has loaded.

| Function | Purpose |
| --- | --- |
| `move_focus(direction)` | Move layout focus or overview selection. |
| `move_window(direction)` | Reorder, join, split, or move a window. |
| `move_window_to_monitor(direction)` | Move a tiled window directly to an adjacent monitor and follow focus. |
| `swap_window(direction)` | Exchange active windows in adjacent stacks. |
| `focus_monitor(direction)` | Focus another monitor or change the overview canvas selection. |
| `switch_workspace(target)` | Switch the whole canvas by direction or `"1"`–`"10"`. |
| `set_mode("row" / "col")` | Change layout orientation. |
| `set_window_size(n)` | Set the session-wide visible-stack count, 1–8. |
| `toggle_fullscreen()` | Expand or restore the active stack. |
| `toggle_overview()` | Open overview or accept its current selection. |
| `toggle_overview("accept" / "cancel")` | Explicitly accept or cancel overview. |
| `cancel_overview()` | Cancel overview. |
| `marks_add(name)` / `marks_delete(name)` | Create or remove a named mark. |
| `marks_visit(name)` / `marks_reset()` | Visit a mark or clear all marks. |

Cardinal directions are `"l"`, `"r"`, `"u"`, `"d"`, with aliases `"left"`,
`"right"`, `"up"`, and `"down"` / `"dn"`. Focus and movement also recognize
`"begin"` / `"b"` / `"beginning"` and `"end"` / `"e"`; individual commands
may restrict supported directions as described above.

## Options

Options are registered under `plugin:scroller:*`. The normal build exposes:

| Option | Default | Values |
| --- | --- | --- |
| `column_default_width` | `"2"` | A string count `"1"`–`"8"`, or `"floating"` to use the last floating size. |
| `focus_wrap` | `0` | `0` disables wrapping at lane/stack edges; `1` enables it. |

After the plugin has loaded, these can be set through Lua configuration:

```lua
hl.config({
    plugin = {
        scroller = {
            column_default_width = "3",
            focus_wrap = 0,
        },
    },
})
```

`set_window_size` overrides the initial stack count for the rest of the session.
Old size names such as `onehalf`, `onethird`, `twothirds`, and `maximized` are no
longer recognized and fall back to `2`.

## Migrating an existing installation

Save your work first. Changing plugin configuration can unload the active
layout, and replacing it may rearrange windows.

### From manual loading

Remove `hl.plugin.load(...)` and any startup `hyprctl plugin load` command
for either `hyprscroller.so` or `hyprCanvas.so`. Use the
[hyprpm startup configuration](../README.md#2-configure-startup-and-layout)
instead. Automatic config reload may unload the manual plugin immediately.
Enable the managed plugin only once the old copy is unloaded, or complete the
switch in a fresh session. Keep the old file as a rollback copy until verified.

### From an older hyprpm installation

The rename changes the plugin identifier to `hyprCanvas`. If an update has
already built it, `hyprpm list` may show `hyprCanvas` disabled under a repository
still named `hyprscroller`. Once the old plugin is unloaded, enable the new one:

```sh
hyprpm enable hyprCanvas
```

To replace the old repository record as well, remove and add it again:

```sh
hyprpm remove https://github.com/86kkd/hyprCanvas.git
hyprpm add https://github.com/86kkd/hyprCanvas.git
hyprpm enable hyprCanvas
```

If the old installation used a different repository URL, use the URL shown by
that installation when removing it. Verify with `hyprpm list` and
`hyprctl plugin list`. Do not load hyprscroller and hyprCanvas together: both
register the `scroller` layout and Lua namespace.
