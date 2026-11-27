local bindings, calls, layout = {}, {}, 'canvas'
local function normalize(key)
  local parts = {}
  for p in key:upper():gmatch('[^+]+') do parts[#parts+1] = p:match('^%s*(.-)%s*$') end
  local key = table.remove(parts)
  table.sort(parts)
  return table.concat(parts, '+') .. ':' .. key
end
local function dispatcher(name)
  return function(args) return { name = name, args = args } end
end
hl = {
  get_config = function() return layout end,
  unbind = function(key) bindings[normalize(key)] = nil end,
  bind = function(key, action, opts)
    key = normalize(key)
    bindings[key] = bindings[key] or {}
    table.insert(bindings[key], { action = action, opts = opts })
  end,
  dispatch = function(action) calls[#calls+1] = action end,
  dsp = {
    focus = dispatcher('focus'), exec_cmd = dispatcher('exec'), layout = dispatcher('layout'),
    window = { move = dispatcher('move'), swap = dispatcher('swap'), fullscreen_state = dispatcher('fullscreen') },
    workspace = { move = dispatcher('move_workspace') },
  },
  plugin = { canvas = setmetatable({}, { __index = function(_, name)
    return function(arg) calls[#calls+1] = { name = name, args = arg } end
  end }) },
}
local function press(key, name, arg)
  local list = bindings[normalize(key)]
  assert(list and #list == 1, key .. ': missing or duplicate')
  calls = {}
  local action = list[1].action
  if type(action) == 'function' then action() else hl.dispatch(action) end
  assert(#calls == 1 and calls[1].name == name, key .. ': wrong action')
  if arg then assert(calls[1].args == arg, key .. ': wrong direction') end
  return calls[1].args
end
-- Seed old/default conflicts and unrelated shortcuts.
for _, mods in ipairs({'SUPER', 'SUPER ALT', 'SUPER CTRL', 'SUPER CTRL ALT', 'SUPER SHIFT', 'CTRL SHIFT'}) do
  for _, key in ipairs({'H','J','K','L','N','M','comma','period'}) do
    hl.bind(mods:gsub(' ', ' + ') .. ' + ' .. key, function() error('stale binding') end)
  end
end
local untouched = function() end
hl.bind('SUPER + Y', untouched)
for _, mode in ipairs({'canvas', 'canvas', 'master', 'dwindle', 'canvas'}) do
  layout = mode
  dofile('examples/canvas-layout.lua')
  for _, spec in ipairs({{'H','N','l','left'},{'J','M','d','down'},{'K','comma','u','up'},{'L','period','r','right'}}) do
    local key, space, dir, native = table.unpack(spec)
    if mode == 'canvas' then
      press('SUPER + '..key, 'move_focus', dir)
      press('SUPER + ALT + '..key, 'move_window', dir)
      press('SUPER + SHIFT + '..key, 'swap_window', dir)
      press('SUPER + CTRL + '..key, 'focus_monitor', dir)
      press('SUPER + CTRL + ALT + '..key, 'move_window_to_monitor', dir)
      assert(not bindings[normalize('CTRL + SHIFT + '..key)])
      press('SUPER + '..space, 'switch_workspace', dir)
      press('SUPER + ALT + '..space, 'move_window_to_workspace', dir)
      assert(not bindings[normalize('SUPER + CTRL + '..space)])
      assert(not bindings[normalize('SUPER + CTRL + ALT + '..space)])
      assert(not bindings[normalize('SUPER + SHIFT + '..space)])
    else
      assert(press('SUPER + '..key, 'focus').direction == native)
      assert(press('SUPER + ALT + '..key, 'swap').direction == native)
      assert(press('SUPER + CTRL + '..key, 'move').direction == native)
      assert(not bindings[normalize('SUPER + ALT + '..space)])
      assert(not bindings[normalize('SUPER + CTRL + '..space)])
      assert(not bindings[normalize('SUPER + CTRL + ALT + '..space)])
      assert(not bindings[normalize('SUPER + SHIFT + '..space)])
      assert(not bindings[normalize('SUPER + CTRL + ALT + '..key)])
    end
  end
  if mode == 'canvas' then
    press('SUPER + I', 'toggle_overview')
    press('SUPER + F', 'toggle_fullscreen')
    calls = {}; bindings[normalize('SUPER + SHIFT + code:10')][1].action(); assert(#calls == 0)
  else
    press('SUPER + I', 'layout', 'addmaster')
    press('SUPER + F', 'fullscreen')
    assert(press('CTRL + SHIFT + H', 'move').workspace == '-1')
    assert(press('CTRL + SHIFT + L', 'move').workspace == '+1')
    assert(not bindings[normalize('CTRL + SHIFT + J')])
    assert(not bindings[normalize('CTRL + SHIFT + K')])
    assert(not bindings[normalize('SUPER + SHIFT + H')])
    assert(not bindings[normalize('SUPER + SHIFT + J')])
    press('SUPER + SHIFT + K', 'exec')
    assert(not bindings[normalize('SUPER + SHIFT + L')])
    press('SUPER + N', 'exec')
    press('SUPER + M', 'layout', 'splitratio 0.3 exact')
    assert(press('SUPER + comma', 'focus').workspace == 'e-1')
    assert(press('SUPER + period', 'focus').workspace == 'e+1')
  end
  assert(bindings[normalize('SUPER + Y')][1].action == untouched)
  print('PASS: ' .. mode)
end
