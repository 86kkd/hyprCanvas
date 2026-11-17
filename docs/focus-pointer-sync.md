# Focus and pointer synchronization

`src/core/focus_pointer_sync.cpp` owns deferred pointer synchronization for
both layouts. Layout code remains responsible for choosing the focused window
and committing its geometry.

- `Navigation` requests synchronization after a successful focus operation.
- `NewWindow` waits until mapping has assigned focus and geometry.
- `Preserve` cancels outstanding synchronization without moving the pointer.

Only the newest request may execute. Mouse motion or button input cancels a
pending request; a changed pointer position also invalidates it. Execution
requires a live window that is still focused and has valid goal geometry.
No request forces a background or no-focus window to become focused.

The position-only warp deliberately bypasses `cursor.no_warps` for explicit
plugin synchronization. It does not synthesize input activity or explicitly
change cursor visibility. Do not replace it with simulated mouse motion.

The existing boolean focus APIs are compatibility adapters: `true` submits
`Navigation`, while `false` preserves the pointer. Window mapping submits
`NewWindow` through `sync_new_window_pointer`. Overview accept and restoration
continue through the same focus adapters. Mouse-driven focus handlers should
not submit navigation requests.

Plugin initialization installs input listeners. Shutdown cancels the idle
callback and removes listeners before plugin code is unloaded.

Run `ctest --test-dir Debug --output-on-failure` and
`python3 scripts/repro-overview-shared-workspace.py --no-warps --reload-plugin`
for focus, mapping, rapid navigation, pointer override, and reload regressions.
The integration test checks geometry and focus; it does not measure cursor
visibility flicker or inject physical pointer input.
