-- 从仓库根目录启动（需要 kitty 和 Debug/karta.so）：
-- env -u HYPRLAND_INSTANCE_SIGNATURE start-hyprland -- -c tests/karta-test.lua
-- Ctrl+Alt 为测试修饰键，减少与外层桌面的 Super 快捷键冲突。
local source = debug.getinfo(1, "S").source:sub(2)
local root = assert(source:match("^(.*)tests/[^/]+$"), "Cannot locate repository from test config")
if root:sub(1, 1) ~= "/" then
    root = assert(os.getenv("PWD"), "PWD is required for a relative config path") .. "/" .. root
end

hl.plugin.load(root .. "Debug/karta.so")
-- 使用输出的首选尺寸；新 lane 根据实际逻辑宽高选择 row / col。
hl.monitor({ output = "", mode = "preferred", position = "auto", scale = 1 })
hl.config({
    general = { layout = "karta", gaps_in = 4, gaps_out = 8, border_size = 2 },
    decoration = { rounding = 6 },
    misc = { disable_hyprland_logo = true, disable_splash_rendering = true },
    input = { kb_layout = "us" },
    debug = { disable_logs = false },
})

local function bind(key, action, description)
    hl.bind("CTRL + ALT + " .. key, action, { description = description })
end

bind("RETURN", function() hl.exec_cmd("kitty") end, "Test: new terminal")
bind("SHIFT + Q", function() hl.dispatch(hl.dsp.exit()) end, "Test: exit session")
bind("O", function() hl.plugin.karta.toggle_overview() end, "Test: open/accept overview")
bind("ESCAPE", function() hl.plugin.karta.cancel_overview() end, "Test: cancel overview")
bind("F", function() hl.plugin.karta.toggle_fullscreen() end, "Test: expand/restore stack")
bind("R", function() hl.plugin.karta.set_mode("row") end, "Test: horizontal lanes")
bind("C", function() hl.plugin.karta.set_mode("col") end, "Test: vertical lanes")

for _, spec in ipairs({ { "LEFT", "l" }, { "RIGHT", "r" }, { "UP", "u" }, { "DOWN", "d" } }) do
    local key, direction = spec[1], spec[2]
    bind(key, function() hl.plugin.karta.move_focus(direction) end, "Test: focus " .. direction)
    bind("SHIFT + " .. key, function() hl.plugin.karta.move_window(direction) end,
        "Test: move/join/split window " .. direction)
    bind("SUPER + " .. key, function() hl.plugin.karta.focus_monitor(direction) end,
        "Test: monitor / overview karta " .. direction)
end

for number = 1, 8 do
    local count = number
    bind(tostring(count), function() hl.plugin.karta.set_window_size(count) end,
        "Test: visible stacks " .. count)
end
bind("TAB", function() hl.plugin.karta.switch_workspace("r") end, "Test: next karta")
bind("SHIFT + TAB", function() hl.plugin.karta.switch_workspace("l") end, "Test: previous karta")
