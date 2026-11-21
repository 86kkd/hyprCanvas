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

Numbers `"1"` through `"10"` address canvas creation order, **not native
Hyprland workspace IDs**. Missing canvases are created with separate backing
workspaces for each monitor. During overview, switching changes the preview;
accepting the selection switches the actual desktop. Outside overview, it
switches immediately.

For a fuller set of bindings, copy
[`examples/scroller-workspaces.lua`](../examples/scroller-workspaces.lua) into
`~/.config/hypr/lua/scroller-workspaces.lua`, then load it after your normal
keybinding modules:

```lua
dofile(os.getenv("HOME") .. "/.config/hypr/lua/scroller-workspaces.lua")
```

The module binds `Ctrl + Shift + H / J / K / L` to `swap_window`
(left / down / up / right). In row mode, adjacent-stack swaps use H/L; in
column mode they use J/K. Other layouts use native directional swapping.

Read the module before installing it. It replaces Tab, number, and wheel
workspace shortcuts, adds directional movement and monitor/overview controls,
and disables numbered window transfers and whole-workspace monitor moves while
using `scroller`. Other layouts retain native behavior. It also overrides the
README's `SUPER + 2` / `SUPER + 3` sizing shortcuts; choose different sizing keys.

Native workspace commands, gestures, and external workspace widgets still use
Hyprland's per-monitor semantics. Route ordinary canvas switching through
`switch_workspace` to keep all monitors together.

[`ChangeLayout.sh`](../examples/ChangeLayout.sh) switches between `master`,
`dwindle`, and `scroller` and installs the associated bindings. Its Lua path
also reloads the workspace module above, so install both files when using it.

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
