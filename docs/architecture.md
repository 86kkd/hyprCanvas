# Architecture Guide

This document is the newcomer-oriented map of the codebase. It is meant to
answer one question first:

How does a Hyprland event or keybinding end up moving windows inside
karta?

A Chinese version lives in [`architecture-cn.md`](./architecture-cn.md).

Related decisions:

- Planned work tracker: [`planned-work.md`](./planned-work.md)
- Feature plan: topology neighbor index: [`topology-neighbor-index.md`](./topology-neighbor-index.md)

## Runtime At A Glance

Most of the project can be understood as one pipeline:

1. Hyprland loads the plugin through `PLUGIN_INIT` in
   [`src/plugin/entry.cpp`](../src/plugin/entry.cpp).
2. `PLUGIN_INIT` registers:
   - config values under `plugin:karta:*`
   - Lua commands under `hl.plugin.karta.*` such as `move_focus`
   - `KartaLayout` as the tiled algorithm named `karta`
   - overview renderer hooks plus focus/deck pointer helpers
3. Hyprland later calls either:
   - Lua functions in [`src/plugin/dispatch/`](../src/plugin/dispatch/)
     when the user presses a keybinding
   - tiled-algorithm callbacks on `KartaLayout` when windows
     are created, removed, moved, or relaid out
4. `KartaLayout` routes that request to its current `Lane`.
5. Each `Lane` routes the operation to one or more `Stack` objects.
6. `Stack` updates window membership, active selection, and geometry.

If you only want the shortest reading path through the project, follow files in
this order:

1. [`src/plugin/entry.cpp`](../src/plugin/entry.cpp)
2. [`src/plugin/dispatch/registration.cpp`](../src/plugin/dispatch/registration.cpp)
   and [`src/plugin/dispatch/layout_commands.cpp`](../src/plugin/dispatch/layout_commands.cpp)
3. [`src/layout/karta/layout.h`](../src/layout/karta/layout.h)
4. [`src/layout/karta/core.cpp`](../src/layout/karta/core.cpp)
5. [`src/layout/karta/focus.cpp`](../src/layout/karta/focus.cpp)
6. [`src/layout/karta/commands.cpp`](../src/layout/karta/commands.cpp)
7. [`src/layout/lane/core.cpp`](../src/layout/lane/core.cpp),
   [`src/layout/lane/actions.cpp`](../src/layout/lane/actions.cpp), and
   [`src/layout/lane/geometry.cpp`](../src/layout/lane/geometry.cpp)
8. [`src/model/stack/core.cpp`](../src/model/stack/core.cpp),
   [`src/model/stack/membership.cpp`](../src/model/stack/membership.cpp), and
   [`src/model/stack/geometry.cpp`](../src/model/stack/geometry.cpp)

## Ownership Hierarchy

```text
Karta Workspace              one whole multi-monitor desktop (plugin-side grouping)
  └─ Hyprland workspace × N   one member per monitor, N = monitor count
        └─ KartaLayout       the `karta` instance of that workspace
              └─ Lane → Stack → window
```

- A **Karta Workspace** (`KartaWorkspaceRepository`, `KartaWorkspaceRecord`,
  the karta cards in overview) groups one ordinary Hyprland workspace per
  monitor so a whole desktop is switched at once. It is an index and a
  switching policy, never the owner of windows.
- Members are exclusive: an ordinary workspace belongs to at most one Karta
  Workspace (`make_members_exclusive`). Every connected monitor has exactly one
  member (`fill_monitor_members`); members of a disconnected monitor are kept
  for when it returns.
- A special workspace is an overlay. It is never recorded as a member
  (`visible_members`), even while it is open on a monitor.
- The **Hyprland workspace** stays authoritative for visibility, focus, and
  window ownership.
- A **`KartaLayout`** belongs to exactly one Hyprland workspace
  (`KartaLayout::getLayoutWorkspace()`) and owns that workspace's lanes.
  Despite its name it is not a Karta Workspace, and nothing links a Karta
  Workspace directly to lanes.
- Single-monitor commands such as `move_focus` and `move_window` enter through
  the `KartaLayout` of the member workspace on the current monitor. Crossing a
  monitor hands off to the `KartaLayout` of the member on the other monitor.

## Core Concepts

There are four main runtime objects for the default `karta` layout.

### Plugin

The plugin is the Hyprland-facing shell around the project. It lives mostly in
[`src/plugin/entry.cpp`](../src/plugin/entry.cpp).

Its job is to:

- initialize logging and layout/karta repositories
- register config values
- register Lua commands via `dispatchers::addLuaFunctions()`
- register `KartaLayout` as the `karta` tiled algorithm
- initialize overview renderer hooks, focus-pointer sync, and deck-pointer
  hit testing

It does not contain layout policy itself.

### KartaLayout

`KartaLayout` is the top-level controller for one Hyprland workspace.
Its public definition is in
[`src/layout/karta/layout.h`](../src/layout/karta/layout.h).

You can think of it as the bridge between Hyprland and the plugin's own layout
model.

`KartaLayout` is responsible for:

- owning the ordered list of `Lane` objects in one workspace
- translating Hyprland callbacks into model operations
- exposing command-facing methods such as `moveFocus` and `moveWindow`
- coordinating cross-lane and cross-monitor handoff
- keeping caches and active-lane state synchronized with Hyprland focus

### Lane

A `Lane` is the next level down. It groups one ordered strip of stacks inside a
karta.

In row mode, a lane behaves like the horizontal sequence you scroll across.
In column mode, a lane still owns stacks, but the active stack semantics and
movement rules change so vertical stacking becomes the dominant local action.

Lanes are the place where the layout starts to feel "PaperWM-like": they decide
which stack is active, which adjacent stack should receive a moved window, and
how stack geometry fits inside the visible karta.

### Stack

A `Stack` owns an ordered list of windows and the state attached to that local
group.

It is responsible for:

- which window is active inside the stack
- adding, removing, and restoring window payloads
- size policy for that local group
- geometry calculations for the windows it owns

If a `Lane` answers "which stack should handle this operation?", `Stack`
answers "what exactly happens to the windows inside that stack?"

### Window

The actual Hyprland window object still comes from Hyprland. Karta keeps
lightweight bookkeeping around it, but the compositor remains the source of
truth for the real client.

`karta` is the only tiled algorithm this plugin registers. The former
`kartagrid` cell layout was removed; directional routing stays on
lane/stack (see [`topology-neighbor-index.md`](./topology-neighbor-index.md)).

## Main Flows

### Startup

Startup begins in [`src/plugin/entry.cpp`](../src/plugin/entry.cpp).

Read `PLUGIN_INIT` first. It does these things in order:

1. sets up logging
2. initializes layout and karta repositories
3. registers config values
4. registers Lua commands under `hl.plugin.karta.*`
5. registers the `karta` tiled algorithm
6. initializes overview renderer hooks plus focus/deck pointer helpers

Config registration happens before tiled-algorithm registration on purpose:
Hyprland may create layout instances as soon as the algorithms are available,
and those instances may already need plugin config values.

After that, Hyprland is free to call the plugin through Lua command invocations
or tiled-layout callbacks.

### New Tiled Window

This is the simplest lifecycle to trace:

1. Hyprland creates a tiled target.
2. `KartaLayout::newTarget` or `KartaLayout::movedTarget` is called.
3. Those functions forward to `KartaLayout::onWindowCreatedTiling`.
4. `onWindowCreatedTiling` finds or creates the active lane.
5. The lane inserts the new window into its active stack according to the
   current mode.
6. Ownership caches are updated so future lookups can jump directly from window
   to lane.

The main implementation lives in
[`src/layout/karta/core.cpp`](../src/layout/karta/core.cpp).

### Focus Movement

Directional focus starts in a Lua command:

1. A keybinding calls `hl.plugin.karta.move_focus("l")` or similar.
2. [`src/plugin/dispatch/layout_commands.cpp`](../src/plugin/dispatch/layout_commands.cpp)
   parses the direction and resolves the active `KartaLayout`.
3. `KartaLayout::moveFocus` builds a `TopologyIndex` (lane, stack, and
   window coordinates of the focused item) and asks
   `KartaLayoutInternal::neighbor` where the direction leads:
   - a local target inside the current lane
   - an adjacent lane
   - a temporary empty lane at the karta edge
   - a cross-monitor handoff
   - nothing
4. The chosen lane or target monitor is activated and the compositor focus is
   updated.

The main implementation lives in
[`src/layout/karta/focus.cpp`](../src/layout/karta/focus.cpp).

### Window Movement

Directional window movement starts similarly:

1. A keybinding calls `hl.plugin.karta.move_window(...)`.
2. The Lua command resolves the active `KartaLayout`.
3. `KartaLayout::moveWindow` asks the same `neighbor` query with the
   move-window intent, which decides whether the window:
   - reorders its stack, joins a neighboring stack, or splits into a new one
   - moves into an adjacent lane or a new lane at the karta edge
   - moves to another monitor
4. Payload extraction and insertion helpers move the active window while
   preserving the source stack or lane state when possible.

The main implementation lives in
[`src/layout/karta/commands.cpp`](../src/layout/karta/commands.cpp) and
[`src/layout/lane/actions.cpp`](../src/layout/lane/actions.cpp).

### Overview

Overview is a separate read-only navigation layer.

Its logic is split into:

- overview session/model/scene/render code under
  [`src/overview`](../src/overview)
- snapshot building inside `KartaLayout`
- overview-specific tests under [`tests/`](../tests/)

The important thing to remember is that overview does not mutate the real lane
or stack geometry while it is open. It navigates a logical snapshot.

## How The Source Tree Is Split

### `src/plugin/entry.cpp`

Plugin ABI entrypoints and one-time registration.

### `src/plugin/dispatch`

String/Lua argument parsing and routing from `hl.plugin.karta.*` into
strongly typed layout methods.

- `registration.cpp`: bootstrap that calls the two registration groups
- `layout_commands.cpp`: layout commands such as `move_focus` and `move_window`
- `overview_mark_commands.cpp`: overview and marks commands
- `shared.h`: shared helpers for resolving the active workspace/layout

### `src/layout/karta`

Workspace-level control plane for `karta`.

- `core.cpp`: target callbacks, workspace ownership, lane cache, lifecycle
- `snapshot.cpp`: workspace layout snapshot capture, persistence, and restore
- `workspace_controller.cpp` / `workspace_controller_logic.cpp`: multi-monitor
  karta workspace activation, direct desktop switching, and cross-karta window
  transfer (shared with overview acceptance so `Overview::Session` stays read-only)
- `focus.cpp`: focus routing and cross-monitor focus handoff
- `commands.cpp`: command-facing mutating operations such as movewindow
- `topology_route.cpp`: the read-only topology index and the `neighbor`
  decision shared by `move_focus` and `move_window`; the index is rebuilt from
  the lane list for every directional command, so it never goes stale
- `dispatch_logic.cpp` and `route_logic.cpp`: pure logic helpers that keep
  decision-heavy code unit-testable

### `src/layout/lane`

Lane-level behavior.

- `core.cpp`: lane ownership and active-stack bookkeeping
- `actions.cpp`: stack/window movement and transfer actions
- `geometry.cpp`: lane-local geometry work

### `src/model/stack`

Local stack data structures.

- `core.cpp`: stack construction and control flow helpers
- `membership.cpp`: add/remove/restore/extract window membership logic
- `geometry.cpp`: sizing, fitting, and placement behavior

### `src/overview`

Overview session, model, navigation, scene, and render hooks.

### `src/core`

Shared geometry helpers, snapshots, direction parsing, marks storage, and
Hyprland runtime adapters.

## Tests

The test target does not boot Hyprland. It focuses on the pure logic that is
worth keeping stable across refactors.

- [`tests/core_logic_tests.cpp`](../tests/core_logic_tests.cpp)
  covers shared helpers and low-level logic.
- [`tests/layout_logic_tests.cpp`](../tests/layout_logic_tests.cpp)
  covers dispatch and handoff decision helpers.
- [`tests/topology_route_tests.cpp`](../tests/topology_route_tests.cpp)
  covers directional neighbor routing for focus and window movement.
- [`tests/layout_snapshot_tests.cpp`](../tests/layout_snapshot_tests.cpp)
  covers layout snapshot behavior.
- overview coverage is split across
  [`tests/overview_logic_tests.cpp`](../tests/overview_logic_tests.cpp),
  [`tests/overview_model_tests.cpp`](../tests/overview_model_tests.cpp),
  [`tests/overview_scene_tests.cpp`](../tests/overview_scene_tests.cpp),
  [`tests/overview_session_logic_tests.cpp`](../tests/overview_session_logic_tests.cpp),
  and [`tests/overview_session_effects_tests.cpp`](../tests/overview_session_effects_tests.cpp).

When you change real stateful behavior in `KartaLayout`, `Lane`, or `Stack`,
the automated tests are necessary but not sufficient. Pair them with the manual
[smoke test checklist](./smoke-test-checklist.md) and, when needed, the
[Hyprland testing notes](./hyprland-testing.md). For changes that need nested
repro scripts or staged debug work, also follow the
[feature/debug workflow](./feature-debug-workflow.md).

## Suggested Reading Strategy

If you are new to the project, use this order:

1. Read this file once without opening code.
2. Open [`src/plugin/entry.cpp`](../src/plugin/entry.cpp) and find `PLUGIN_INIT`.
3. Open [`src/plugin/dispatch/layout_commands.cpp`](../src/plugin/dispatch/layout_commands.cpp)
   and trace one command, such as `dispatch_movefocus` / `lua_move_focus`.
4. Open [`src/layout/karta/layout.h`](../src/layout/karta/layout.h) and read
   the public methods on `KartaLayout`.
5. Trace one lifecycle end to end:
   - new window: `newTarget -> onWindowCreatedTiling`
   - focus: `hl.plugin.karta.move_focus -> dispatch_movefocus -> moveFocus`
   - move window: `hl.plugin.karta.move_window -> dispatch_movewindow -> moveWindow`
6. Only then dive into lane and stack code.

That order keeps the mental model top-down. If you start at `Stack` first, the
local code will make sense, but the overall runtime picture usually will not.

## Hyprland 0.57 Note

Hyprland 0.57 no longer registers plugin dispatchers. Keybindings must call
Lua functions under `hl.plugin.karta`, not the older `karta:movefocus`
dispatcher style. Plugin config values such as `plugin:karta:focus_wrap`
are still registered separately through `addConfigValueV2`.
