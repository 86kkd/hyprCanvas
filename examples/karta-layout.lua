-- Load AFTER your default/user keybind modules, with the updated plugin loaded.
-- Normal native workspaces remain available when another layout is selected.
local function karta()
  return hl.get_config("general.layout") == "karta"
end

local function bind(key, action, description)
  hl.unbind(key)
  hl.bind(key, action, { description = description })
end

local function switch(key, karta, native)
  bind(key, function()
    if karta() then
      hl.plugin.karta.switch_workspace(karta)
    else
      hl.dispatch(hl.dsp.focus({ workspace = native }))
    end
  end, "switch whole karta workspace")
end

local function move(key, direction, native, follow)
  bind(key, function()
    if karta() then
      -- Reorder locally or hand off to another physical monitor's visible
      -- workspace. Never dispatch movetoworkspace +/-1 from this path.
      hl.plugin.karta.move_window(direction)
    else
      hl.dispatch(hl.dsp.window.move({ workspace = native, follow = follow }))
    end
  end, "move window within visible karta")
end

switch("SUPER + tab", "r", "m+1")
switch("SUPER + SHIFT + tab", "l", "m-1")
switch("SUPER + mouse_down", "r", "e+1")
switch("SUPER + mouse_up", "l", "e-1")
-- Install only the active layout's bindings. Reload this module after changing
-- layouts (ChangeLayout.sh does this); unbind first so repeated loads are safe.
local function layout_bind(key, action, description, native, native_description)
  hl.unbind(key)
  if karta() then
    hl.bind(key, action, { description = description })
  elseif native then
    hl.bind(key, native, { description = native_description or description })
  end
end

local native_shift_window_keys = {
  K = hl.dsp.exec_cmd(os.getenv("HOME") .. "/.config/hypr/scripts/KeyBinds.sh"),
}

for _, spec in ipairs({
  { "H", "l", "left" }, { "J", "d", "down" },
  { "K", "u", "up" }, { "L", "r", "right" },
}) do
  local key, direction, native = spec[1], spec[2], spec[3]
  layout_bind("SUPER + " .. key, function()
    hl.plugin.karta.move_focus(direction)
  end, "focus window " .. native .. " (karta)",
    hl.dsp.focus({ direction = native }), "focus window " .. native)
  layout_bind("SUPER + ALT + " .. key, function()
    hl.plugin.karta.move_window(direction)
  end, "move window " .. native .. " (karta)",
    hl.dsp.window.swap({ direction = native }), "swap window " .. native)
  layout_bind("SUPER + SHIFT + " .. key, function()
    hl.plugin.karta.swap_window(direction)
  end, "swap window " .. native .. " (karta)",
    native_shift_window_keys[key], "search keybinds")

  layout_bind("SUPER + CTRL + " .. key, function()
    hl.plugin.karta.focus_monitor(direction)
  end, "focus monitor " .. native .. " (karta)",
    hl.dsp.window.move({ direction = native }), "move window " .. native)
  layout_bind("SUPER + CTRL + ALT + " .. key, function()
    hl.plugin.karta.move_window_to_monitor(direction)
  end, "move window to monitor " .. native .. " (karta)")

  -- Release Ctrl+Shift+H/J/K/L back to applications in karta.
  -- Native layouts restore the user's H/L workspace transfer shortcuts.
  local transfer = direction == "l" and "-1" or direction == "r" and "+1" or nil
  hl.unbind("CTRL + SHIFT + " .. key)
  if not karta() and transfer then
    bind("CTRL + SHIFT + " .. key,
      hl.dsp.window.move({ workspace = transfer, follow = true }),
      "move window to " .. transfer .. " workspace")
  end
end

layout_bind("SUPER + F", function()
  hl.plugin.karta.toggle_fullscreen()
end, "expand / restore stack (karta)",
  hl.dsp.window.fullscreen_state({ internal = 2, client = 0, action = "toggle" }),
  "special fullscreenstate")
layout_bind("SUPER + I", function()
  hl.plugin.karta.toggle_overview()
end, "toggle overview (karta)", hl.dsp.layout("addmaster"), "add master")

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
      -- A numbered destination means another global karta, not a monitor.
      -- Numbered native transfers bypass karta membership. Use the
      -- directional move_window_to_workspace shortcut in karta instead.
      if not karta() then
        hl.dispatch(hl.dsp.window.move({ workspace = native, follow = follow }))
      end
    end, "move to native workspace (other layouts only)")
  end
end

-- Preserve monitor ownership: moving an entire native workspace to a different
-- monitor bypasses the karta mapping. Keep these bindings for other layouts.
for _, spec in ipairs({ {"F9", "left"}, {"F10", "right"}, {"F11", "up"}, {"F12", "down"} }) do
  bind("SUPER + CTRL + " .. spec[1], function()
    if not karta() then
      hl.dispatch(hl.dsp.workspace.move({ monitor = spec[2] }))
    end
  end, "move native workspace (other layouts only)")
end

-- Native defaults for the keys claimed by spatial navigation. These restore
-- the local desktop's night-light / split-ratio and workspace shortcuts.
local native_space_keys = {
  n = hl.dsp.exec_cmd(os.getenv("HOME") .. "/.config/hypr/scripts/Hyprsunset.sh toggle"),
  m = hl.dsp.layout("splitratio 0.3 exact"),
  comma = hl.dsp.focus({ workspace = "e-1" }),
  period = hl.dsp.focus({ workspace = "e+1" }),
}
for _, spec in ipairs({ {"n", "l"}, {"m", "d"}, {"comma", "u"}, {"period", "r"} }) do
  local key, direction = spec[1], spec[2]
  layout_bind("SUPER + " .. key, function()
    hl.plugin.karta.switch_workspace(direction)
  end, "karta workspace " .. direction .. " (karta)", native_space_keys[key],
    "native " .. key .. " shortcut")
  layout_bind("SUPER + ALT + " .. key, function()
    hl.plugin.karta.move_window_to_workspace(direction)
  end, "move window to karta " .. direction .. " (karta)")
  hl.unbind("SUPER + CTRL + " .. key)
  hl.unbind("SUPER + CTRL + ALT + " .. key)
end

-- Unbind unused Shift+lower-row shortcuts (former monitor aliases and unused
-- notification/music presets).
for _, key in ipairs({ "n", "m", "comma", "period" }) do
  hl.unbind("SUPER + SHIFT + " .. key)
end
