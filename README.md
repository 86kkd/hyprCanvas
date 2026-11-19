# Hyprscroller

Stacks with more than two windows automatically use an overlapping card layout:
the active window occupies 61.8% of the visible stack axis, the previously
focused window 19.1%, and the remaining windows equally share 19.1%. Windows
retain their original order: moving down leaves the previous window above the
active card; moving up leaves it below. Without focus history, an adjacent
window becomes secondary. Row mode stacks vertically; column mode stacks
horizontally. Scroller fullsize (`mainMod+F`) with multiple windows gives the
focused window 90% of the monitor workarea along the stack axis and its full
cross-axis span. Two windows use 90% / 10%; with three or more, the previous
window gets 5% and the rest equally share 5%. All background windows have edge
labels. Switching focus transfers the large region to the new main window;
leaving fullsize restores ordinary proportions. Native fullscreen and maximized
windows retain their normal behavior.
Returning to one or two windows restores the ordinary layout and saved size
settings. Card placement is automatic while the stack contains three or more
windows; per-window size settings apply again outside that mode.
Moving a window toward a stack expanded with the scroller fullscreen toggle
places it in a separate stack on the requested side instead of joining the
fullsize stack. The destination retains its fullsize and restore geometry.
Deck movement and resizing use Hyprland's existing window animation settings;
the plugin does not register or override animation curves.
Narrow background cards show a larger 18px title in an opaque rounded rectangle
(up to 34px high, scaled down when necessary) on the exposed edge:
upper-left above the active card, lower-left below it. Long titles are ellipsized;
column mode places the tags vertically on the exposed left/right edges.
Background strips divide their fixed 19.1% share equally regardless of count. Pointer focus uses only the currently visible
strip or foreground area, tracking native window animations rather than the
covered full-window rectangles. Narrow side slivers do not steal pointer focus.


[Hyprscroller](https://github.com/86kkd/hyprscroller) is a
[Hyprland](https://hyprland.org) layout plugin that creates a window layout
similar to [PaperWM](https://github.com/paperwm/PaperWM). I initialy
learned how to write a *Hyprland* plugin from [hyprslidr](https://gitlab.com/magus/hyprslidr),
which is a similar idea.

This repository is an actively maintained fork of the original
[`dawsers/hyprscroller`](https://github.com/dawsers/hyprscroller), which is now
archived.

![Intro](./videos/hyprscroller.gif)

The plugin is quite feature complete and supports gaps, borders, special
workspace, scroller-managed fullscreen expansion, overview, marks and
installation through `hyprpm`.

This fork is maintained against the Hyprland versions I actually run and test.
The focus is practical compatibility with packaged Hyprland releases first,
instead of chasing trunk continuously.

The current compatibility baseline is Arch Linux Hyprland 0.57.0. Rebuild the
plugin after every Hyprland package update, and restart the Hyprland session
before loading that new build.

## Requirements

*hyprscroller* supports the version of *Hyprland* I use, which should be the
same as the Arch Linux `hyprland` package. You can try your luck with the
latest `git` changes, but I will be slower to keep up with those, as there are
too many API changes going on upstream.

Aside from those versions, even though the feature set will be frozen for
them, *hyprscroller* also supports v0.35, v0.38.1, v0.39.1 and v0.40.0.


## Building and installing

With Hyprland installed it should be as simple as running

``` sh
# builds a shared object hyprscroller.so
make all
# installs the shared library in ~/.config/hypr/plugins
make install
```

If you are hacking on the plugin, you can also run the pure logic tests without
starting Hyprland:

``` sh
cmake -S . -B Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build ./Debug -j
ctest --test-dir ./Debug --output-on-failure
```

For compositor-level regressions after touching canvas or lane hot paths, run
the manual [smoke test checklist](./docs/smoke-test-checklist.md) as well.

## For Developers

If you are trying to understand how the project works internally, start with
the newcomer-oriented [architecture guide](./docs/architecture.md).

For repository workflow and commit writing, see the
[commit convention](./docs/commit-convention.md).

The shortest useful reading path is:

1. [`src/plugin/entry.cpp`](./src/plugin/entry.cpp)
2. [`src/dispatchers.cpp`](./src/dispatchers.cpp)
3. [`src/layout/canvas/layout.h`](./src/layout/canvas/layout.h)
4. [`src/layout/canvas/core.cpp`](./src/layout/canvas/core.cpp)
5. [`src/layout/canvas/focus.cpp`](./src/layout/canvas/focus.cpp)
6. [`src/layout/canvas/commands.cpp`](./src/layout/canvas/commands.cpp)

The automated tests are now split by subsystem instead of living in one large
file:

- [`tests/core_logic_tests.cpp`](./tests/core_logic_tests.cpp)
- [`tests/layout_logic_tests.cpp`](./tests/layout_logic_tests.cpp)
- [`tests/overview_logic_tests.cpp`](./tests/overview_logic_tests.cpp)

For stateful layout changes, pair those tests with:

- [smoke test checklist](./docs/smoke-test-checklist.md)
- [Hyprland testing notes](./docs/hyprland-testing.md)

For the locally maintained Hyprland layout-switch helper script that wires
`master` / `dwindle` / `scroller` and the current scroller-specific binds, see
[examples/ChangeLayout.sh](./examples/ChangeLayout.sh).

A more automated option is to use `hyprpm`.

``` sh
hyprpm add https://github.com/86kkd/hyprscroller
# verify it installed correctly
hyprpm list
```

You can enable or disable it via `hyprpm enable hyprscroller` and
`hyprpm disable hyprscroller`.


## Configuration

Hyprland 0.57 removed the legacy `plugin = ...` config keyword. If you are not
using `hyprpm`, load the plugin from your Lua configuration:

``` lua
hl.plugin.load(os.getenv("HOME") .. "/.config/hypr/plugins/hyprscroller.so")
```

Instead, if you use `hyprpm`, it should be as simple as adding this to
your `~/.config/hypr/hyprland.lua`:

``` lua
hl.on("hyprland.start", function()
    hl.exec_cmd("hyprpm reload -n")
end)
```

To turn on the layout, use

``` lua
hl.config({ general = { layout = "scroller" } })
```

For manual runtime loading, use `hyprctl plugin load /path/to/hyprscroller.so`.
This is a plugin-management command, not a dispatcher. Add `-i <instance-signature>`
when targeting a nested test instance. Do not load the same plugin twice.


## Lua commands

Hyprland 0.57 no longer registers plugin dispatchers. The plugin exposes the
following functions under `hl.plugin.scroller` instead:

| Lua function | Description |
|--------------|-------------|
| `move_focus(direction)` | Move focus inside the layout, or move the overview selection. |
| `focus_monitor(direction)` | Move monitor focus, or select another canvas in overview. |
| `move_window(direction)` | Move the active window in the requested direction. |
| `cycle_size(step)` | Cycle the active stack size with `next` or `prev`. |
| `set_mode(mode)` | Select `row` or `col` mode. |
| `toggle_fullscreen()` | Toggle scroller-managed fullscreen expansion. |
| `toggle_overview(action)` / `cancel_overview()` | Open, accept, or cancel overview. |
| `marks_add(name)` / `marks_delete(name)` | Add or delete a mark. |
| `marks_visit(name)` / `marks_reset()` | Visit a mark or clear all marks. |

## Fullscreen behavior

`scroller:togglefullscreen` is not the same as Hyprland's native
`fullscreen/fullscreenstate` dispatchers.

- In *row* mode, it expands the active column horizontally so the focused
  window becomes as wide as a single-window workspace.
- In *column* mode, it expands the active window vertically so it becomes as
  tall as a single-window workspace.
- It stays inside scroller's layout model, so focus movement and scrolling
  behavior continue to work normally.


## Modes

*Hyprscroller* works in one of two modes that can be changed at any moment.

1. *row* mode: it is the default. It creates new windows in a new column.
   `cyclesize` affects the width of the active column.

2. *column* mode: It creates new windows in the current column, right below the
   active window. `cyclesize` affects the height of the active window.


## Window/Column Focus and Movement

If you want to use *Hyprscroller* you will need to map your key bindings from
the default `movefocus`/`movewindow` to
`scroller:movefocus`/`scroller:movewindow`. The reason is *Hyprland* doesn't
have the concept (yet) of a workspace that spans more than the space of a
monitor, and when focusing directionally, it doesn't look for windows that are
"outside" of that region. If this changes in the future, these two dispatches
may become obsolete.

`movefocus` and `movewindow` accept the following directional arguments:
`l` or `left`, `r` or `right`, `u` or `up`, `d` or `dn` or `down`, `b` or
`begin` or `beginning`, `e` or `end`. So you can focus or move windows/columns
in a direction or to the beginning or end or the row.


## Resizing

`cyclesize` accepts an argument which is either `+1`/`1`/`next`, or
`-1`/`prev`/`previous`. It cycles forward or backward through three column
widths (in *row* mode): one third, one half or two thirds of the available
width of the monitor. In *column* mode, the fractions are relative to the
height of the monitor, and are: one third, one half, two thirds or one.
However, using `resizewindow`, you can modify the width or height of
any window freely.


## Building and Splitting Stacks

`move_window` along the lane's stack axis (left/right in *row* mode, up/down in
*column* mode) handles stack membership:

- a single-window stack joins the neighboring stack in that direction;
- the active window of a multi-window stack is split out into a new stack on
  that side.


## Overview

`scroller:toggleoverview` toggles a bird's eye overlay across the visible
monitors. Overview is now a read-only navigation layer:

1. Real window geometry is left untouched while overview is open.
2. `scroller:movefocus` moves the logical overview selection instead of real focus.
3. `scroller:focusmonitor` moves the focused canvas workspace preview while overview stays open.
4. Closing overview accepts the currently focused target/canvas.
5. `scroller:toggleoverview cancel` or `scroller:canceloverview` restores the
   original workspace/window.

With hold-to-preview bindings, that means you can hold your main modifier,
open overview, use `scroller:focusmonitor` on `N/M/,/.` to change canvas
workspace focus, and release the modifier to enter the currently focused
canvas workspace.

Overview can target:

1. Existing tiled windows.
2. Existing empty workspaces.
3. Synthetic empty-workspace targets created by directional navigation.

The renderer now draws stable workspace cards instead of live window previews,
so overview should be used as a monitor/workspace/window picker, not as a mode
for editing window state in place.


## Marks

You can use *marks* to navigate to frequently used windows, regardless of
which workspace they are in (it even works for the special workspace windows).

`scroller:marksadd` adds a named mark. Use a *submap* to create bindings for
several named marks you may want to use. See the configuration example for
directions.

`scroller:marksdelete` deletes a named mark created with `scroller:marksadd`.

`scroller:marksvisit` moves the focus to a previously created mark.

`scroller:marksreset` clears all marks.

Marks reference windows, but are global, they may belong to different
workspaces, so visiting a mark may switch workspaces.

You can use any string name for a mark, for example in scripts. But they are
also very convenient to use with regular key bindings by simply using a letter
as the name. Again, see the example configuration.


## Options

*hyprscroller* currently accepts the following options:

1. `column_default_width`: determines the width of new columns in *row* mode.
Possible arguments are: `onehalf` (default), `onethird`, `twothirds`,
`maximized`, `floating` (uses the default width set by the application).

2. `focus_wrap`: determines whether focus will *wrap* when at the first or
last window of a row/column. Possible arguments are: `true`|`1` (default), or
`false`|`0`.

For example:

``` conf
plugin {
    scroller {
        column_default_width = onehalf
        focus_wrap = false
    }
}
```


## Key bindings

On Hyprland 0.57, bind the plugin Lua functions after calling
`hl.plugin.load(...)`. The callback resolves the plugin function when the key is
pressed, after asynchronous plugin loading has completed:

``` lua
hl.bind("SUPER + LEFT", function() hl.plugin.scroller.move_focus("l") end)
hl.bind("SUPER + RIGHT", function() hl.plugin.scroller.move_focus("r") end)
hl.bind("SUPER + CTRL + LEFT", function() hl.plugin.scroller.move_window("l") end)
hl.bind("SUPER + CTRL + RIGHT", function() hl.plugin.scroller.move_window("r") end)
hl.bind("SUPER + BRACKETLEFT", function() hl.plugin.scroller.set_mode("row") end)
hl.bind("SUPER + BRACKETRIGHT", function() hl.plugin.scroller.set_mode("col") end)
hl.bind("SUPER + TAB", function() hl.plugin.scroller.toggle_overview() end)
```

The repository's [ChangeLayout helper](./examples/ChangeLayout.sh) installs the
same Lua-compatible bindings dynamically when switching layouts.

### Legacy pre-0.57 configuration

The dispatcher-based examples below apply only to older Hyprland releases and
are retained as a migration reference. They do not register on Hyprland 0.57.

``` conf
# Move focus with mainMod + arrow keys
bind = $mainMod, left, scroller:movefocus, l
bind = $mainMod, right, scroller:movefocus, r
bind = $mainMod, up, scroller:movefocus, u
bind = $mainMod, down, scroller:movefocus, d
bind = $mainMod, home, scroller:movefocus, begin
bind = $mainMod, end, scroller:movefocus, end

# Movement
bind = $mainMod CTRL, left, scroller:movewindow, l
bind = $mainMod CTRL, right, scroller:movewindow, r
bind = $mainMod CTRL, up, scroller:movewindow, u
bind = $mainMod CTRL, down, scroller:movewindow, d
bind = $mainMod CTRL, home, scroller:movewindow, begin
bind = $mainMod CTRL, end, scroller:movewindow, end

# Modes
bind = $mainMod, bracketleft, scroller:setmode, row
bind = $mainMod, bracketright, scroller:setmode, col

# Sizing keys
bind = $mainMod, equal, scroller:cyclesize, next
bind = $mainMod, minus, scroller:cyclesize, prev

# Resize submap
# will switch to a submap called resize
bind = $mainMod SHIFT, R, submap, resize
# will start a submap called "resize"
submap = resize
# sets repeatable binds for resizing the active window
binde = , right, resizeactive, 100 0
binde = , left, resizeactive, -100 0
binde = , up, resizeactive, 0 -100
binde = , down, resizeactive, 0 100
# use reset to go back to the global submap
bind = , escape, submap, reset
# will reset the submap, meaning end the current one and return to the global one
submap = reset

# overview keys
# bind key to toggle overview (normal)
bind = $mainMod, tab, scroller:toggleoverview
# overview submap
# will switch to a submap called overview
bind = $mainMod, tab, submap, overview
# will start a submap called "overview"
submap = overview
bind = , right, scroller:movefocus, right
bind = , left, scroller:movefocus, left
bind = , up, scroller:movefocus, up
bind = , down, scroller:movefocus, down
# use reset to go back to the global submap
bind = , escape, scroller:toggleoverview,
bind = , escape, submap, reset
bind = , return, scroller:toggleoverview,
bind = , return, submap, reset
bind = $mainMod, tab, scroller:toggleoverview,
bind = $mainMod, tab, submap, reset
# will reset the submap, meaning end the current one and return to the global one
submap = reset

# Marks
bind = $mainMod, M, submap, marksadd
submap = marksadd
bind = , a, scroller:marksadd, a
bind = , a, submap, reset
bind = , b, scroller:marksadd, b
bind = , b, submap, reset
bind = , c, scroller:marksadd, c
bind = , c, submap, reset
bind = , escape, submap, reset
submap = reset

bind = $mainMod SHIFT, M, submap, marksdelete
submap = marksdelete
bind = , a, scroller:marksdelete, a
bind = , a, submap, reset
bind = , b, scroller:marksdelete, b
bind = , b, submap, reset
bind = , c, scroller:marksdelete, c
bind = , c, submap, reset
bind = , escape, submap, reset
submap = reset

bind = $mainMod, apostrophe, submap, marksvisit
submap = marksvisit
bind = , a, scroller:marksvisit, a
bind = , a, submap, reset
bind = , b, scroller:marksvisit, b
bind = , b, submap, reset
bind = , c, scroller:marksvisit, c
bind = , c, submap, reset
bind = , escape, submap, reset
submap = reset

bind = $mainMod CTRL, M, scroller:marksreset
```

### Global canvas workspace bindings (Hyprland 0.57 Lua)

`hl.plugin.scroller.switch_workspace("l"|"r"|"u"|"d"|"1".."10")`
switches the complete canvas: one native backing workspace per connected monitor.
Numbers address canvas creation order, **not** native Hyprland workspace IDs.
Missing canvases are created with separate backing workspaces on every monitor.
While overview is open this changes only the preview; acceptance switches all monitors.
Outside overview it switches immediately. Directional `move_window` stays within the
visible canvas and can hand windows between physical monitors.

Load [examples/scroller-workspaces.lua](examples/scroller-workspaces.lua) **after**
your existing keybind modules and after updating the plugin. It replaces Tab,
number and wheel workspace switches with canvas switches; Ctrl+Shift+H/L and
bracket window moves use directional movement. Shift/Ctrl+number window transfers
and whole native workspace-to-monitor moves are inactive under scroller, preserving
the monitor-only movement policy. Other layouts retain native behavior. Special
scratchpads are separate from ordinary canvas workspaces.

The Lua branch of `ChangeLayout.sh` reloads
`~/.config/hypr/lua/scroller-workspaces.lua` after each layout change (including
`init`). Install that module as well as the shell script, and load it after your
user bindings in `hyprland.lua` so config reloads preserve the overrides. The
module also removes duplicate native comma/period workspace bindings before
installing scroller monitor/overview actions.
Native `hl.dsp.focus({workspace=...})`, workspace gestures and external workspace
widgets still use Hyprland's per-monitor semantics; route ordinary workspace
navigation through the plugin API when using the global canvas model.

Legacy canvas records that share a native workspace are normalized when the
current visible canvas is captured. The visible canvas retains its backing
workspaces and windows; duplicate references in other canvases receive fresh,
empty backing workspace IDs. This prevents an unchanged monitor when switching
between groups imported from earlier native per-monitor navigation.
