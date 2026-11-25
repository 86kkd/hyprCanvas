-- Load AFTER your default/user keybind modules, with the updated plugin loaded.
-- Normal native workspaces remain available when another layout is selected.
local function scroller()
  return hl.get_config("general.layout") == "scroller"
end

local function bind(key, action, description)
  hl.unbind(key)
  hl.bind(key, action, { description = description })
end

local function switch(key, canvas, native)
  bind(key, function()
    if scroller() then
      hl.plugin.scroller.switch_workspace(canvas)
    else
      hl.dispatch(hl.dsp.focus({ workspace = native }))
    end
  end, "switch whole canvas workspace")
end

local function move(key, direction, native, follow)
  bind(key, function()
    if scroller() then
      -- Reorder locally or hand off to another physical monitor's visible
      -- workspace. Never dispatch movetoworkspace +/-1 from this path.
      hl.plugin.scroller.move_window(direction)
    else
      hl.dispatch(hl.dsp.window.move({ workspace = native, follow = follow }))
    end
  end, "move window within visible canvas")
end

switch("SUPER + tab", "r", "m+1")
switch("SUPER + SHIFT + tab", "l", "m-1")
switch("SUPER + mouse_down", "r", "e+1")
switch("SUPER + mouse_up", "l", "e-1")
-- Install only the active layout's bindings. Reload this module after changing
-- layouts (ChangeLayout.sh does this); unbind first so repeated loads are safe.
local function layout_bind(key, action, description, native, native_description)
  hl.unbind(key)
  if scroller() then
    hl.bind(key, action, { description = description })
  elseif native then
    hl.bind(key, native, { description = native_description or description })
  end
end

for _, spec in ipairs({
  { "H", "l", "left" }, { "J", "d", "down" },
  { "K", "u", "up" }, { "L", "r", "right" },
}) do
  local key, direction, native = spec[1], spec[2], spec[3]
  layout_bind("SUPER + " .. key, function()
    hl.plugin.scroller.move_focus(direction)
  end, "focus window " .. native .. " (scroller)",
    hl.dsp.focus({ direction = native }), "focus window " .. native)
  layout_bind("SUPER + ALT + " .. key, function()
    hl.plugin.scroller.move_window(direction)
  end, "move window " .. native .. " (scroller)",
    hl.dsp.window.swap({ direction = native }), "swap window " .. native)
  layout_bind("SUPER + CTRL + " .. key, function()
    hl.plugin.scroller.switch_workspace(direction)
  end, "canvas workspace " .. native .. " (scroller)",
    hl.dsp.window.move({ direction = native }), "move window " .. native)

  layout_bind("SUPER + CTRL + ALT + " .. key, function()
    hl.plugin.scroller.move_window_to_workspace(direction)
  end, "move window to canvas " .. native .. " (scroller)")

  -- The user's native H/L bindings transfer a window between workspaces.
  -- J/K have no native binding: remove the shortcut outside scroller entirely.
  local transfer = direction == "l" and "-1" or direction == "r" and "+1" or nil
  layout_bind("CTRL + SHIFT + " .. key, function()
    hl.plugin.scroller.swap_window(direction)
  end, "swap window " .. native .. " (scroller)",
    transfer and hl.dsp.window.move({ workspace = transfer, follow = true }) or nil,
    "move window to " .. (transfer or "") .. " workspace")
end

layout_bind("SUPER + F", function()
  hl.plugin.scroller.toggle_fullscreen()
end, "expand / restore stack (scroller)",
  hl.dsp.window.fullscreen_state({ internal = 2, client = 0, action = "toggle" }),
  "special fullscreenstate")
layout_bind("SUPER + I", function()
  hl.plugin.scroller.toggle_overview()
end, "toggle overview (scroller)", hl.dsp.layout("addmaster"), "add master")

for _, spec in ipairs({ {"bracketleft", "l", "-1"}, {"bracketright", "r", "+1"} }) do
  move("SUPER + SHIFT + " .. spec[1], spec[2], spec[3], true)
  move("SUPER + CTRL + " .. spec[1], spec[2], spec[3], false)
end

for number = 1, 10 do
  local native = tostring(number)
  local code = "code:" .. tostring(number + 9)
  switch("SUPER + " .. code, native, native)
  for _, mods in ipairs({ "SHIFT", "CTRL" }) do
    local follow = mods == "SHIFT"
    bind("SUPER + " .. mods .. " + " .. code, function()
      -- A numbered destination means another global canvas, not a monitor.
      -- Numbered native transfers bypass canvas membership. Use the
      -- directional move_window_to_workspace shortcut in scroller instead.
      if not scroller() then
        hl.dispatch(hl.dsp.window.move({ workspace = native, follow = follow }))
      end
    end, "move to native workspace (other layouts only)")
  end
end

-- Preserve monitor ownership: moving an entire native workspace to a different
-- monitor bypasses the canvas mapping. Keep these bindings for other layouts.
for _, spec in ipairs({ {"F9", "left"}, {"F10", "right"}, {"F11", "up"}, {"F12", "down"} }) do
  bind("SUPER + CTRL + " .. spec[1], function()
    if not scroller() then
      hl.dispatch(hl.dsp.workspace.move({ monitor = spec[2] }))
    end
  end, "move native workspace (other layouts only)")
end

-- Native defaults for the keys claimed by monitor navigation. These restore
-- the local desktop's night-light / split-ratio and workspace shortcuts.
local native_monitor_keys = {
  n = hl.dsp.exec_cmd(os.getenv("HOME") .. "/.config/hypr/scripts/Hyprsunset.sh toggle"),
  m = hl.dsp.layout("splitratio 0.3 exact"),
  comma = hl.dsp.focus({ workspace = "e-1" }),
  period = hl.dsp.focus({ workspace = "e+1" }),
}
for _, spec in ipairs({ {"n", "l"}, {"m", "d"}, {"comma", "u"}, {"period", "r"} }) do
  local key, direction = spec[1], spec[2]
  layout_bind("SUPER + " .. key, function()
    hl.plugin.scroller.focus_monitor(direction)
  end, "focus monitor " .. direction .. " (scroller)", native_monitor_keys[key],
    "native " .. key .. " shortcut")
  layout_bind("SUPER + ALT + " .. key, function()
    hl.plugin.scroller.move_window_to_monitor(direction)
  end, "move window to monitor " .. direction .. " (scroller)")
end

-- Retire the former Shift+monitor movement aliases. Keep the desktop's normal
-- notification/music shortcuts available, including while using scroller.
for _, key in ipairs({ "n", "m", "comma", "period" }) do
  hl.unbind("SUPER + SHIFT + " .. key)
end
bind("SUPER + SHIFT + N", hl.dsp.exec_cmd("swaync-client -t -sw"), "notification panel")
bind("SUPER + SHIFT + M",
  hl.dsp.exec_cmd(os.getenv("HOME") .. "/.config/hypr/UserScripts/RofiBeats.sh"), "online music")
