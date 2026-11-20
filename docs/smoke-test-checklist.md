# Hyprscroller Smoke Test Checklist

Use this checklist after changing `src/layout/canvas/*`, `src/layout/lane/*`, or `src/model/stack.*`.

## Preconditions

- Build the plugin with `make debug`.
- Start Hyprland with the plugin enabled.
- Prefer two monitors for the full pass: one landscape and one portrait if available.
- Prepare one normal workspace and one special workspace.

## Checklist

1. Open three tiled windows in `row` mode on one workspace and confirm the newest window appears next to the latest focused window, not at the far end.
2. Run `scroller:movefocus l/r` across the row and confirm focus changes stay inside the lane until the edge.
3. Run `scroller:movewindow l/r` inside the same row and confirm window order changes without losing geometry or leaving blank space.
4. Switch to `column` mode, add at least three windows to one stack, and confirm `scroller:movefocus u/d` moves inside the stack correctly.
5. In `column` mode, use `move_window` up/down to join a single-window stack into its neighbor and to split a window back out of a multi-window stack; confirm the moved window keeps focus and no window disappears.
6. In `row` mode, run `hl.plugin.scroller.set_window_size(3)` and then `set_window_size(2)`; confirm every column (including ones on other workspaces and newly opened ones) takes a third, then a half, of the workarea.
7. Toggle `scroller:togglefullscreen` in `row` mode, then move focus left/right and confirm the layout restores cleanly after toggling back.
8. Toggle `scroller:togglefullscreen` in `column` mode and confirm expanded-window behavior still allows focus movement and relayout after exit.
9. Create or enter an empty ephemeral lane via directional focus and confirm it is cleaned up once focus returns to a non-empty lane.
10. Move a window to an adjacent lane and confirm the source lane is removed only when empty.
11. Move a window from the landscape monitor to the portrait monitor and back with `scroller:movewindow`, confirming target insertion happens beside the latest focused window on the destination monitor.
12. Move focus across monitors with `scroller:movefocus` and confirm focus lands on the target monitor without bouncing back on the next command.
13. Repeat cross-monitor `movewindow` while the target or crossed neighbor is scroller-fullscreen and confirm there is no half-monitor blank region.
14. Open a special workspace containing scroller windows, toggle it visible/hidden, and confirm relayout still uses the correct monitor.
15. On an empty or newly created workspace, run `scroller:movefocus` or `scroller:movewindow` once and confirm builtin fallback behavior does not crash or wedge focus.
16. Prefer `./scripts/repro-overview.sh --outer-monitor <portrait-monitor>` for overview regressions, then confirm every visible monitor shows overview cards without live window geometry jumping.
17. While overview is open, run `scroller:movefocus` across windows, empty workspaces, and between monitors, then confirm the real focused window does not change until accept.
18. Close overview with `scroller:toggleoverview accept` and `scroller:canceloverview`, confirming accept jumps to the selected target while cancel restores the original workspace/window.
19. Open overview on a monitor with no tiled workspaces and confirm the monitor still renders a stable empty-state card instead of crashing or drawing stale previews.

## Notes

- For `overview`, render, or navigation work, follow the step-by-step process in
  [feature-debug-workflow.md](./feature-debug-workflow.md) instead of relying on
  ad-hoc manual reproduction.
- If any step fails, capture the exact dispatcher, workspace, monitor orientation, and whether the active lane was empty/fullscreen/special.
- Re-run steps 10-15 after any change touching cross-monitor handoff, dispatcher helpers, or focus suppression.
- Re-run steps 16-19 after any change touching `src/overview/*`, renderer integration, or overview accept/cancel flow.

## Ordered card stacks

- Put four windows A/B/C/D in one stack, in that order. Focus B, then C:
  C is fully visible, B remains above C and partially exposed (19.1% of the stack axis), A/D expose narrow edges.
- Focus D, then C: C is fully visible, D remains below C and partially exposed (19.1% of the stack axis),
  A/B expose narrow edges. Repeat C/B/C to verify direction reversals.
- Click an exposed card and verify that it expands without changing the spatial
  order; repeated focus notifications must not change the secondary card.
- Leave the stack for the next lane at its boundary, then return. Confirm the
  stack order is unchanged; no wraparound is required for this check.
- Close or expel the secondary window, then continue navigating. Confirm a
  surviving neighbor becomes partially exposed (19.1% of the stack axis) and no stale window is referenced.
- Reduce the stack to two windows and confirm ordinary layout returns. Add a
  third window and confirm card layout resumes. Repeat in column mode, where
  the same ordering and exposure rules apply horizontally.
- Toggle fullscreen/maximize and exit, and inspect overview before and after a
  focus change. Confirm the active card and projected geometry agree.

### Card transition animation

- Switch B → C → B and inspect intermediate frames: position and size should
  follow the configured Hyprland animation, without repeated geometry resets.
- Reverse direction several times before the animation completes. Each movement
  should continue from the current rendered position and settle on the last
  selected window.
- Disable global animations, then separately disable `windowsMove`: both should
  make deck transitions immediate. Re-enable and confirm animated transitions.
- Expel a card, reduce the deck to two windows, switch layouts, and unload the
  plugin. Confirm all windows continue using the configured Hyprland animations.

### Narrow-edge title tags

- With four cards, focus B then C: only the narrow A/D cards have title tags,
  at A's upper-left and D's lower-left. Reverse focus and check that the main
  and secondary cards have no tags.
- Change a background window's title; confirm its tag updates. Test a long title
  and a non-Latin title; neither should wrap or spill outside the narrow strip.
- During a focus animation, tags follow their windows and remain occluded by
  foreground cards. Open overview and confirm there are no duplicate live tags.
- Repeat in column mode and at fractional monitor scale. Expel a tagged window,
  reduce the stack to two windows, and unload the plugin; tags should disappear.

### Pointer focus in card stacks

- Enable `input.follow_mouse = 1`. Move slowly around the center of the active
  card: covered siblings must never take focus.
- Move into each exposed top/bottom strip, then make 1px movements throughout
  the transition. Focus should stay on the selected card without oscillating.
- Enter a secondary window and verify it can receive focus. Repeat with
  disabled animations, in column mode, and with fractional monitor scaling.
- Use keyboard focus after pointer selection, leave the stack, and open a
  floating window/menu over it. Native keyboard, floating and popup focus should
  remain intact.
- Close/expel windows until only two remain, change layouts, then unload/reload
  the plugin. Confirm hit areas return to ordinary full-window layout behavior.

- Check progressive cross-axis scaling in both row and column, landscape and
  portrait: with six cards and the last two as main/secondary, sizes from main
  outward are 100%, 95%, 85.5%, 76.95%, 69.255%, 62.3295%.
  Focus a middle card: other-card layers restart independently on each side.
  Repeat in fullsize, including two cards (100% / 95% across the cross axis).
  Check shortened title tags remain readable/ellipsized, exposed strips remain
  clickable, and side slivers do not steal focus during animation.
- Check deck visible shares: primary 61.8%, secondary 19.1%, all remaining
  strips together 19.1%, both axes and after changing window count.
- Check narrow-edge labels have opaque rounded backgrounds and larger text;
  long titles ellipsize and tiny strips do not spill into adjacent windows.

### Fullsize insertion boundaries

Automated isolated regression: `python3 scripts/repro-fullsize-insertion.py`.

- Expand a single-window stack with the scroller fullscreen toggle (`mainMod+F`).
  From its right neighbor, move a window left: it must join the fullsize target
  deck while the target stays expanded. Repeat rightward from the left neighbor.
- The joined stack must keep fullsize geometry. Toggle fullsize off and confirm
  ordinary multi-window geometry is restored.
- Split the joined window back out with `move_window`; the remaining fullsize
  stack must stay expanded, and the expelled window restores the fullsize state
  its own stack had before joining (ordinary stays ordinary, fullsize stays
  fullsize).
- Repeat in column mode with up/down.
- Cross-lane/monitor insertion into an expanded target must also join that deck
  without clearing its fullsize state.

### Focused-window fullsize decks

Automated isolated regression: `python3 scripts/repro-fullsize-deck.py`
(requires `grim` in addition to the insertion regression dependencies).

- With 2, 3 and 5 windows in one stack, toggle scroller fullsize. The main
  window must occupy 90% of the stack axis and the entire cross axis.
- With 2 windows the secondary gets 10%; otherwise the secondary gets 5%, and
  the other windows equally share 5%. Keep original order during focus changes.
- Check all background strips show labels at their exposed edges, and pointer
  focus uses only visible regions in both orientations.
- Toggle fullsize off and verify ordinary window geometry is restored. Close
  background windows down to one and verify the remaining window fills the area.

### Overview first directional move inside a stack

Automated isolated regression: `python3 scripts/repro-overview-stack-navigation.py`.

- Place a multi-window row stack between two other stacks. Select each member in
  overview and press left/right once: selection must reach the corresponding
  neighboring stack, without first jumping to a vertically stacked sibling.
- Repeat after re-entering overview and in column mode with up/down.
- At a navigation edge, an aligned sibling must not become a target because of
  floating-point projection drift; movement along the stack axis still works.
