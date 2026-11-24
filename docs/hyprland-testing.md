# Hyprland 测试说明

这份文档说明如何用 `Hyprland` 本身来做 `hyprscroller` 的集成测试，
重点覆盖以下场景：

- 在不污染当前桌面会话的前提下复现插件问题
- 用 debug 构建的插件做手工回归
- 采集 `Hyprland` 和 `hyprscroller` 的日志
- 对 `layout` 切换、`overview`、跨 workspace/monitor 行为做定点回归

## 目标

推荐优先使用“嵌套 Hyprland”方式测试，而不是直接在你正在使用的主会话里反复热加载。

这样做的好处：

- 插件崩溃时不会把你当前桌面一起带崩
- 可以单独抓一份 `Hyprland` stdout/stderr
- 可以明确区分测试实例和主实例

## 1. 构建 Debug 插件

Arch Linux 更新 `hyprland` 包后，先确认磁盘上的版本和当前运行中的版本一致：

```bash
Hyprland --version
hyprctl version
```

如果两条命令显示的版本或 commit 不同，先退出并重新登录 Hyprland 会话，再构建和
加载插件。Hyprland 插件 ABI 绑定到具体构建；不能把针对新头文件编译的插件加载到
尚未重启的旧 Hyprland 进程中。嵌套回测也应在主会话重启后进行，避免新旧
Aquamarine/EGL 运行库混用导致 nested compositor 在插件初始化前崩溃。

先在仓库根目录构建 debug 版本：

```bash
make debug
```

构建完成后，测试用插件路径是：

```bash
./Debug/hyprCanvas.so
```

插件初始化成功后，`~/.hyprland/plugins/hyprscroller/hyprscroller.log` 中的
`hyprland_abi` 字段会记录该二进制的编译目标 commit 和依赖 ABI，可用于排查加载了
旧构建的问题。

## 使用仓库内的 Lua 测试配置

构建 Debug 插件后，在仓库根目录运行（需安装 `kitty`）：

```bash
env -u HYPRLAND_INSTANCE_SIGNATURE start-hyprland -- -c tests/hyprscroller-test.lua
```

[`tests/hyprscroller-test.lua`](../tests/hyprscroller-test.lua) 加载本仓库的
`Debug/hyprCanvas.so`，启动时不创建窗口，输出尺寸采用 `preferred`。
需要终端时按 `Ctrl + Alt + Enter` 打开 kitty。快捷键使用
`Ctrl + Alt`，方便在嵌套会话中与外层桌面的 `Super` 绑定区分：

| 快捷键 | 操作 |
| --- | --- |
| Ctrl + Alt + Enter | 新建终端 |
| Ctrl + Alt + 方向键 | 移动焦点；overview 中按 stack 导航 |
| Ctrl + Alt + Shift + 方向键 | 移动窗口、合并或拆分 stack |
| Ctrl + Alt + O | 打开 / 接受 overview |
| Ctrl + Alt + Escape | 取消 overview |
| Ctrl + Alt + R / C | 切换 row / col 模式 |
| Ctrl + Alt + F | 展开 / 恢复 stack |
| Ctrl + Alt + 1…8 | 设置可见 stack 数量 |
| Ctrl + Alt + Tab / Shift + Tab | 下一个 / 上一个 canvas |
| Ctrl + Alt + Super + 方向键 | 切换显示器；overview 中切换 canvas 预览 |
| Ctrl + Alt + Shift + Q | 退出测试会话 |

验证 stack 导航：在 row 模式下新建第二个终端，按
`Ctrl + Alt + Shift + Left` 将其并入左侧 stack；再新建一个终端作为相邻
stack。打开 overview 后，上下移动不应切换同一 stack 内的窗口，左右移动
应切换 stack，并选中目标 stack 原来的活动窗口。切换 col 模式后，方向对调。

## 2. 快速复现 overview

关闭窗口后的 stack 布局回归（独立嵌套实例，不重新加载主桌面插件）：

```bash
python3 scripts/repro-stack-removal.py
```

该脚本覆盖 row / col 模式下三窗口 stack 删除成员后恢复普通布局，以及
只剩一个窗口时立即填满 lane；删除后先读取几何，不切换焦点来触发刷新。
同时验证进入空 lane 再返回后，未聚焦 stack 的所有窗口也恢复原坐标，
防止从多 lane 回到单 lane 时残留一屏偏移。

版本说明：本节及其子节中的旧版 shell repro 脚本保留了 Hyprlang 配置和旧
`scroller:*` dispatcher 调用，尚未迁移，不适用于当前 0.57 基线。当前版本
请使用本文末尾的 `scripts/repro-overview-shared-workspace.py`，或第 3 节起的
Lua 手工测试流程。不要直接运行旧脚本来验证当前版本。

如果你要复现“指定 monitor 上的浮动嵌套 Hyprland + 创建窗口 + 打开
overview”这条固定链路，优先直接用仓库里的脚本：

```bash
./scripts/repro-overview.sh --outer-monitor HDMI-A-1
```

脚本会完成这些事情：

- 生成一份最小 nested `Hyprland` 配置并加载 `./Debug/hyprCanvas.so`
- 把嵌套 Hyprland 作为一个浮动窗口放到指定 outer monitor
- 在 nested 实例里创建一个图片预览窗口和四个终端窗口
- 在 nested 实例内部自动执行 `scroller:toggleoverview`
- 默认等待短暂可见期后自动退出 nested 实例
- 输出 nested 实例 id、日志路径和测试结果

如果不传 `--outer-monitor`，脚本会优先选择第一块竖屏 monitor，
否则退回当前 focused monitor。

如果你想保留 nested 实例做手工观察，可以显式传：

```bash
./scripts/repro-overview.sh --outer-monitor HDMI-A-1 --keep-open
```

如果你要复现“overview 里的 `movefocus` 没按视觉上下关系走”的问题，
优先用这条更定向的脚本：

```bash
./scripts/repro-overview-movefocus.sh --outer-monitor HDMI-A-1
```

它会在 nested 会话里构造两个 `column` 模式 workspace，自动执行
`toggleoverview -> movefocus u -> accept`，并把最终落点写到 result 文件里。

如果你要复现“overview 跨 monitor accept 没有真正切到目标窗口”的问题，
优先用这条脚本：

```bash
./scripts/repro-overview-accept-cross-monitor.sh --outer-monitor HDMI-A-1
```

它会在 nested 会话里创建第二块虚拟输出，在第一块 monitor 打开 overview，
将逻辑选中项移动到第二块 monitor 的目标窗口，再执行 accept，并验证
最终 focus 是否真的落到那个目标窗口上。

如果你要验证“overview 里的 `focusmonitor` 会创建 / 选择新的 canvas workspace，
并且关闭 overview 后所有可见 monitor 一起切过去”，优先用这条脚本：

```bash
./scripts/repro-overview-canvas-workspaces.sh --outer-monitor HDMI-A-1
```

它会先验证 normal-mode `scroller:focusmonitor` 只切 monitor focus，
再在 overview 里执行 `focusmonitor r -> toggleoverview(close-accept)`，并检查
5 块 nested monitor 是否一起切到新的空白 canvas workspace，再验证可以切回原
来的 canvas，且未创建窗口的临时 canvas 被回收。脚本会把 nested 输出摆成“上中下三个横屏，左右各一个竖屏”的十字
形拓扑，并同时断言 outer `aquamarine - WAYLAND-1..5` 预览窗口的几何都符合预期。
虚拟屏幕使用实际预览窗口的像素尺寸（竖屏直接使用竖向尺寸，不额外旋转），避免
Wayland configure 更新后屏幕缩小而终端仍停留在旧坐标。默认不创建终端窗口；传入 `--with-test-windows` 时，两个测试终端按各屏尺寸
留出边距并居中，回归也会检查切换 canvas 返回后终端仍完整可见且居中。

脚本使用 Lua 配置和 dispatcher，并在启动时输出目标 monitor、运行目录和日志路径。
不带 `--keep-open` 时自动验证后关闭嵌套会话；手动测试并持续输出日志时运行：

```bash
./scripts/repro-overview-canvas-workspaces.sh --keep-open
```

`--keep-open` 默认只搭建五屏，不自动切换 canvas 或运行回归检查，可以直接开始
手动操作。需要先自动验证再手动测试时加 `--check`；检查失败也会保留会话并输出
结果文件路径，便于排查。自动检查期间请等待结果后再操作，以免改变验证条件。

脚本保持在前台运行并输出本次 Hyprland 日志。按 Ctrl+C 会关闭
本次启动的嵌套 Hyprland 及全部五个虚拟输出；从嵌套会话内部退出也会结束脚本。
其他已运行的 Hyprland 会话不受影响。预览窗口按所属进程的唯一测试配置路径
过滤，已有的同名 `aquamarine - WAYLAND-*` 窗口不会参与计数或被重新定位。

保留会话的快捷键以 `Ctrl+Alt` 为前缀：`Enter` 新建终端，`O` 打开 / 确认
退出 overview，`Escape` 取消 overview，`Super+方向键` 在普通模式切 monitor、
在 overview 切 canvas 预览，`Tab` / `Shift+Tab` 切换 canvas，`Shift+Q` 退出
嵌套会话。普通方向键移动焦点，`Shift+方向键` 移动窗口。

### 2.1 复用 5 屏布局 Helper

如果后面的集成测试也要复用这套 5 屏十字布局，不要在脚本里重复写 monitor 几何，
直接 source 这个 helper：

```bash
source "$REPO_ROOT/scripts/lib/nested-monitor-layouts.sh"
```

这份 helper 提供的入口：

- `hyprscroller_five_monitor_names`
- `hyprscroller_five_outer_titles`
- `hyprscroller_monitor_logical_geometry`
- `hyprscroller_compute_five_monitor_outer_layout`
- `hyprscroller_install_outer_output_map_time_float_rules`
- `hyprscroller_outer_event_socket_path`
- `hyprscroller_start_outer_output_event_watcher`
- `hyprscroller_wait_for_outer_output_event_watcher_ready`
- `hyprscroller_wait_for_outer_output_position_events`
- `hyprscroller_create_positioned_wayland_outputs`
- `hyprscroller_position_outer_windows`
- `hyprscroller_apply_five_monitor_cross_layout`

推荐时序是先根据 outer monitor 计算出 5 个 preview window 的几何，然后在启动
nested Hyprland 之前安装最小 map-time `float` 规则，让后续
`aquamarine - WAYLAND-*` 从 map 的第一刻就是 floating。第一个 nested output 用
和 `scripts/repro-overview.sh` 一样的 exec rule 启动：`monitor; float; size; center`。
之后订阅 outer Hyprland 的 `.socket2.sock` 事件流，watcher 收到 `openwindow`
事件后，立即按 window address 执行 `resizewindowpixel` 和 `movewindowpixel`，
并保留 `setfloating` 作为幂等兜底。创建每个 nested output 后，脚本仍会同步定位
一次，最后应用 nested monitor 的十字拓扑。

这份 helper 只安装 `float` 规则，不安装 map-time `size/move` 规则，也不调用
`hyprctl reload config-only`。事件驱动 watcher 依赖 `python3` 连接 Hyprland 的
Unix event socket。

最小复用方式：

```bash
NESTED_MONITOR_NAMES=()
OUTER_OUTPUT_TITLES=()
OUTER_OUTPUT_XS=()
OUTER_OUTPUT_YS=()
OUTER_OUTPUT_WIDTHS=()
OUTER_OUTPUT_HEIGHTS=()
OUTER_OUTPUT_EVENT_STATE="$RUN_DIR/outer-output-events.tsv"
OUTER_OUTPUT_EVENT_READY="$RUN_DIR/outer-output-events.ready"

hyprscroller_five_monitor_names NESTED_MONITOR_NAMES
hyprscroller_five_outer_titles OUTER_OUTPUT_TITLES

hyprscroller_compute_five_monitor_outer_layout \
  "$outer_monitor_json" \
  "$WINDOW_WIDTH" \
  "$WINDOW_HEIGHT" \
  96 \
  64 \
  100 \
  OUTER_OUTPUT_XS \
  OUTER_OUTPUT_YS \
  OUTER_OUTPUT_WIDTHS \
  OUTER_OUTPUT_HEIGHTS

hyprscroller_install_outer_output_map_time_float_rules OUTER_OUTPUT_TITLES

OUTER_OUTPUT_EVENT_SOCKET="$(hyprscroller_outer_event_socket_path)"
hyprscroller_start_outer_output_event_watcher \
  OUTER_OUTPUT_EVENT_WATCHER_PID \
  "$OUTER_OUTPUT_EVENT_SOCKET" \
  "$OUTER_OUTPUT_EVENT_STATE" \
  "$OUTER_OUTPUT_EVENT_READY" \
  "$OUTER_MONITOR_X" \
  "$OUTER_MONITOR_Y" \
  OUTER_OUTPUT_TITLES \
  OUTER_OUTPUT_XS \
  OUTER_OUTPUT_YS \
  OUTER_OUTPUT_WIDTHS \
  OUTER_OUTPUT_HEIGHTS
hyprscroller_wait_for_outer_output_event_watcher_ready "$OUTER_OUTPUT_EVENT_READY"

hyprscroller_create_positioned_wayland_outputs \
  "$NESTED_INSTANCE" \
  1 \
  5 \
  wait_for_nested_monitor_count \
  wait_for_outer_output_window_count \
  "$OUTER_MONITOR_X" \
  "$OUTER_MONITOR_Y" \
  OUTER_OUTPUT_TITLES \
  OUTER_OUTPUT_XS \
  OUTER_OUTPUT_YS \
  OUTER_OUTPUT_WIDTHS \
  OUTER_OUTPUT_HEIGHTS

hyprscroller_wait_for_outer_output_position_events "$OUTER_OUTPUT_EVENT_STATE" 5

hyprscroller_position_outer_windows \
  "$OUTER_MONITOR_X" \
  "$OUTER_MONITOR_Y" \
  OUTER_OUTPUT_TITLES \
  OUTER_OUTPUT_XS \
  OUTER_OUTPUT_YS \
  OUTER_OUTPUT_WIDTHS \
  OUTER_OUTPUT_HEIGHTS

hyprscroller_apply_five_monitor_cross_layout \
  "$NESTED_INSTANCE" \
  "$WINDOW_WIDTH" \
  "$WINDOW_HEIGHT"
```

这套 helper 默认生成的 nested monitor 拓扑是：

- `WAYLAND-2` 上横屏
- `WAYLAND-3` 中横屏
- `WAYLAND-5` 下横屏
- `WAYLAND-1` 左竖屏
- `WAYLAND-4` 右竖屏

如果你要验证“layout 切换 / plugin reload 之后 scroller 是否恢复原布局”，
优先用这条脚本：

```bash
./scripts/repro-layout-persistence.sh
```

它会在 nested 会话里构造一个多 lane、多窗口的 scroller 布局，然后依次验证：

- `master -> scroller`
- `dwindle -> scroller`
- `plugin unload/load -> scroller`

脚本会输出 nested instance、log 路径、run dir，以及每一步的几何对比结论。

## 3. 手工启动嵌套 Hyprland 测试实例

先写一份最小测试配置，例如 `/tmp/hyprscroller-test.lua`：

```lua
hl.plugin.load("/home/linda/Documents/hyprscroller/Debug/hyprCanvas.so")
hl.monitor({ output = "", mode = "preferred", position = "auto", scale = 1 })
hl.config({
    general = { layout = "scroller", gaps_in = 4, gaps_out = 8, border_size = 2 },
    decoration = { rounding = 6 },
    misc = { disable_hyprland_logo = true, disable_splash_rendering = true },
    input = { kb_layout = "us" },
    debug = { disable_logs = false },
})
```

当前基线使用 Lua 配置，不支持旧的 `plugin = ...`。运行时手动加载可用
`hyprctl -i <instance-signature> plugin load /path/to/hyprCanvas.so`；
这是插件管理命令，不是 dispatcher。不要对已经加载的插件重复加载。

然后直接启动一个嵌套测试实例：

```bash
env -u HYPRLAND_INSTANCE_SIGNATURE start-hyprland -- -c /tmp/hyprscroller-test.lua
```

说明：

- 这个命令会在你当前 Wayland 会话里再起一个 Hyprland
- `start-hyprland` 为 Hyprland 0.56+ 建立 watchdog 和独立 Wayland socket
- `env -u HYPRLAND_INSTANCE_SIGNATURE` 避免嵌套实例继承外层 Hyprland 的实例标识
- 它适合做插件 debug
- 不建议拿它替代日常登录会话

## 4. 找到测试实例

启动后，用下面的命令列出所有实例：

```bash
hyprctl instances -j
```

通常你会看到：

- 主会话的实例
- 新起的嵌套测试实例

建议记住这两个字段：

- `instance`
- `wl_socket`

后续所有命令都尽量带上：

```bash
hyprctl -i <instance-signature> ...
```

不要默认发给当前主会话。

## 5. 在测试实例里创建窗口

例如在测试实例里启动两个 `kitty`：

```bash
hyprctl -i <instance-signature> eval 'hl.dispatch(hl.dsp.exec_cmd("kitty"))'
hyprctl -i <instance-signature> eval 'hl.dispatch(hl.dsp.exec_cmd("kitty"))'
```

查看窗口状态：

```bash
hyprctl -i <instance-signature> clients -j
```

查看当前 workspace：

```bash
hyprctl -i <instance-signature> activeworkspace -j
```

## 6. 手工测试 layout 切换

测试 `general:layout` 切换时，不要改主配置文件，直接对测试实例下发：

```bash
hyprctl -i <instance-signature> eval 'hl.config({general={layout="master"}})'
hyprctl -i <instance-signature> eval 'hl.config({general={layout="dwindle"}})'
hyprctl -i <instance-signature> eval 'hl.config({general={layout="scroller"}})'
```

常见回归点：

- `master -> scroller`
- `dwindle -> scroller`
- `scroller -> master -> scroller`
- 多 workspace 下切换后再回到隐藏 workspace
- 有 `fullscreen` / `overview` 状态时再切 layout

## 7. 手工测试 overview

打开 overview：

```bash
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.toggle_overview()'
```

移动 overview 里的逻辑选中项：

```bash
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.move_focus("r")'
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.move_focus("l")'
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.move_focus("u")'
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.move_focus("d")'
```

移动 overview 里的 canvas workspace focus：

```bash
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.focus_monitor("r")'
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.focus_monitor("l")'
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.focus_monitor("u")'
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.focus_monitor("d")'
```

接受或取消：

```bash
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.toggle_overview()'
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.cancel_overview()'
```

推荐重点回归：

- 单 workspace overview 是否铺满 monitor
- 多 workspace 时横屏/竖屏网格是否合理
- 打开/关闭 overview 时是否有卡片残留
- overview 期间真实 focus 是否被错误改变

## 8. 手工测试 scroller Lua 命令

日常手工回归优先用仓库里的短命令包装，它会自动选中「非当前会话」的嵌套实例：

```bash
./scripts/hsctl which          # 看选中了哪个 instance
./scripts/hsctl kitty          # 在嵌套实例里开终端
./scripts/hsctl focus l
./scripts/hsctl focus r
./scripts/hsctl move d
./scripts/hsctl swap r
./scripts/hsctl monitor r     # 普通模式切 monitor，overview 中切 canvas 预览
./scripts/hsctl workspace r   # 直接切下一个 canvas（也可用 canvas r）
./scripts/hsctl workspace 2   # 切到第 2 个 canvas
./scripts/hsctl marks add editor
./scripts/hsctl marks visit editor
./scripts/hsctl marks delete editor
./scripts/hsctl marks reset
./scripts/hsctl size 3
./scripts/hsctl size 2
./scripts/hsctl mode col
./scripts/hsctl mode row
./scripts/hsctl fullscreen
./scripts/hsctl overview
./scripts/hsctl overview cancel
./scripts/hsctl clients
./scripts/hsctl exit
```

也可以先固定目标实例，后面就不用每次带 `-i`：

```bash
export HYPRSCROLLER_INSTANCE="$(./scripts/hsctl which)"
./scripts/hsctl focus r
```

需要原始 `hyprctl` 时：

```bash
./scripts/hsctl raw monitors -j
./scripts/hsctl eval 'hl.plugin.scroller.move_focus("r")'
```

等价的长命令（一般不必再手写）：

```bash
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.move_focus("r")'
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.move_window("r")'
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.set_mode("row")'
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.set_mode("col")'
hyprctl -i <instance-signature> eval 'hl.plugin.scroller.toggle_fullscreen()'
```

推荐测试组合：

- `row` / `column` 模式切换后再创建窗口
- `togglefullscreen` 后再 `movewindow`
- 跨 lane / 跨 monitor 的 `movefocus`
- 空 workspace / 空 lane 下的 fallback 行为

## 9. 日志采集

### 8.1 Hyprland 日志

如果你是直接在终端里运行：

```bash
env -u HYPRLAND_INSTANCE_SIGNATURE start-hyprland -- -c /tmp/hyprscroller-test.lua
```

那么这个终端里的 stdout/stderr 就是测试实例的 `Hyprland` 日志。

如果要落盘：

```bash
env -u HYPRLAND_INSTANCE_SIGNATURE start-hyprland -- -c /tmp/hyprscroller-test.lua > /tmp/hyprland-test.log 2>&1
```

重点关注：

- 崩溃前最后几行
- `pixman` / `render` / `hook` / `workspace` 相关报错
- layout 切换时的异常输出

### 8.2 插件日志

插件日志默认写到：

```bash
~/.hyprland/plugins/hyprscroller/hyprscroller.log
```

查看末尾日志：

```bash
tail -n 200 ~/.hyprland/plugins/hyprscroller/hyprscroller.log
```

如果要只看本次测试新增的内容，推荐先记住文件大小，再在测试后截取新增部分。

## 10. 截图和录屏

如果你想确认 overview 或布局渲染结果，可以在嵌套实例上截图。

例如测试实例的 socket 是 `wayland-2`：

```bash
WAYLAND_DISPLAY=wayland-2 grim /tmp/hyprscroller-test.png
```

这样可以在不影响主会话的情况下抓到测试实例的画面。

## 11. 清理测试实例

测试结束后，直接让测试实例退出：

```bash
hyprctl -i <instance-signature> eval 'hl.dispatch(hl.dsp.exit())'
```

不要直接对主实例下发这个命令。

## 12. 推荐测试流程

推荐顺序：

1. `make debug`
2. 优先运行 `./scripts/repro-overview.sh --outer-monitor <monitor>`
3. 如果脚本不适用，再手工启动嵌套 `env -u HYPRLAND_INSTANCE_SIGNATURE start-hyprland -- -c /tmp/hyprscroller-test.lua`
4. 用 `hyprctl instances -j` 找到测试实例
5. 在测试实例里创建目标窗口
6. 跑目标场景
7. 失败时同时保存：
   - `Hyprland` 终端日志
   - `~/.hyprland/plugins/hyprscroller/hyprscroller.log`
   - 必要时的截图
8. 用 `hyprctl -i <instance-signature> eval 'hl.dispatch(hl.dsp.exit())'` 退出测试实例

## 13. 建议记录的复现信息

如果某个问题需要继续排查，建议至少记录这些信息：

- 使用的 `Hyprland` 版本
- 使用的插件构建类型：`Debug` 或 `Release`
- 测试实例的配置文件
- 当前 monitor 拓扑：单屏 / 双屏，横屏 / 竖屏
- 复现步骤的 dispatcher 序列
- 是否包含：
  - `fullscreen`
  - `overview`
  - `special workspace`
  - 跨 monitor handoff

## 14. 关联文档

- 手工回归清单见 [smoke-test-checklist.md](./smoke-test-checklist.md)
- 提交规范见 [commit-convention.md](./commit-convention.md)
- 推荐工程流程见 [feature-debug-workflow.md](./feature-debug-workflow.md)

## 15. 排障记录：overview 里的空 workspace 卡片

有一次排查竖屏 monitor 上 `toggleoverview` 时多出“空 workspace 卡片”，
最后确认那不是 overview 自己创建了 phantom workspace，而是主会话里本来就
残留了一个 `special:scratchpad`。

当时的关键现象：

- `hyprctl workspaces -j` 里能看到 `special:scratchpad`
- 这个 workspace 归属到竖屏 monitor
- workspace 里只有 floating window，没有 tiled window
- overview 建模时会先过滤 floating window，再给空 workspace 补一个空 target

这会让人误以为“toggleoverview 额外生成了一个空 workspace”，但真正发生的
事情是：

- 主会话之前某次测试把窗口放进了 `special:scratchpad`
- 测试结束后没有把这个 special workspace 清掉
- overview 读取到这个 workspace 记录后，把它渲染成了空卡片

这次排障里最容易犯错的点是把下面两个状态混为一谈：

- `hyprctl monitors -j` 里的 `specialWorkspace`
- `hyprctl workspaces -j` 里存在的 `special:*` workspace 记录

它们含义不同：

- `monitor.specialWorkspace` 表示这个 monitor 当前正在显示哪个 special workspace
- `workspaces -j` 里的 `special:*` 只表示 Hyprland 会话里存在这个 workspace，
  并且当前归属到某个 monitor

所以：

- 看到 `workspaces -j` 里有 `special:scratchpad`，不代表它此刻正展开显示
- 但只要 overview 的数据源把它纳入模型，它仍然可能变成一张空卡片

建议以后遇到类似问题时按这个顺序确认：

1. `hyprctl monitors -j`，确认 special workspace 是否真的处于显示状态
2. `hyprctl workspaces -j`，确认是否有残留的 `special:*` workspace 记录
3. `hyprctl clients -j`，确认该 workspace 里是否只有 floating window
4. 如果是测试遗留状态，先清理 special workspace，再判断是否真有 overview bug

## 三 workspace / 副屏 overview 消失回归（Hyprland 0.57 Lua）

```bash
cmake --build Debug -j4
ctest --test-dir Debug --output-on-failure
python3 scripts/repro-overview-shared-workspace.py
```

依赖 `Hyprland`、`hyprctl`、`kitty`、`grim` 和 Python Pillow。脚本启动独立
compositor，按 PID 找到自己的 instance，创建两块 headless 输出后移除其
Wayland 预览输出。启动时会短暂出现 nested 窗口；测试期间不切换主会话
workspace、不热加载主会话插件，结束时只关闭自己的实例。
输出 `/tmp/hyprscroller-shared-workspace.*/` 下的截图、日志和 result.json。
这是 Lua 配置/API 路径，不依赖旧脚本中的 legacy dispatcher。

复现顺序：记录 `(101,102)` canvas，原生切换主屏到 103 并记录
`(103,102)`，再回到 `(101,102)` 打开 overview。旧动画缓存只用 window
指针作 key，第二份副屏窗口预览把第一份坐标覆盖到屏幕外，副屏变成纯背景。
修复后缓存身份包含 canvas、monitor 和 window。另一个回归从共享的 102
窗口打开 overview，确保初始选择限定在当前 canvas，不接受历史 canvas。

脚本先验证正常双屏截图，再检查三 workspace 的红/绿窗口像素和共享 origin
的蓝/绿窗口像素；还检查方向/编号 canvas 切换的所有 monitor 成员、窗口
方向移动不产生新 workspace、最终窗口仍在当前可见 canvas。
历史原生 workspace 组合仍可被读取，不迁移或删除用户窗口。

配套快捷键见 `examples/scroller-workspaces.lua`，需要在用户绑定之后加载。
主会话配置和运行中的插件不会由回归脚本自动更新。

### no_warps 下的跨屏焦点与鼠标同步

`python3 scripts/repro-overview-shared-workspace.py --no-warps` 会在独立实例中
启用 `cursor.no_warps=true` 和 `input.follow_mouse=0`，同时重跑 overview /
canvas 测试，验证跨屏移动保持目标窗口焦点、鼠标同步到目标窗口内，以及下一次
`move_focus` 在目标显示器执行。插件主动请求的导航和新窗口同步会明确绕过
`cursor.no_warps`；新窗口只有实际获得焦点后才同步鼠标。连续导航只执行最新的
同步请求，鼠标移动或点击会取消尚未执行的请求。

鼠标坐标同步与光标是否显示是不同的行为：实现只更新位置，不合成鼠标输入，
也不主动改变光标隐藏状态。当前脚本验证焦点和坐标，不验证光标是否闪现，
也不注入物理鼠标输入。策略与生命周期见 [焦点与鼠标同步](focus-pointer-sync.md)。

键盘 action workspace 和跨屏焦点验证必须使用 Hyprland 的 focused monitor，
不能拿 cursor monitor 代替。

### 安装前的热重载验证

添加 `--reload-plugin` 会在独立实例中切到 dwindle，卸载/加载待安装插件，
再切回 scroller，断言所有窗口的 workspace/monitor 归属和可见 workspace
不变。切换布局可能改变焦点，因此安装流程应另外记录并恢复活动窗口。
脚本同时覆盖 Lua 配置中的 `hl.plugin.load(...)` 自动加载入口。

### 旧 canvas 共享成员迁移

除了预览可见性，回归必须断言不同普通 canvas 不能引用同一个原生 workspace。
典型旧状态是 `(1,2)`、`(1,4)`、`(1,3)`，切换时左屏仍显示 workspace 1。
迁移以实际可见 canvas 为优先（不能信任可能滞后的 ACTIVE 字段），保留当前
窗口；其他组的重复引用分配新的空 workspace ID，已有唯一隐藏成员保持不变。
脚本现在还实际在旧第 1/2 组间切换，断言每块 monitor 的 workspace ID 都变化，
再返回原组。纯测试覆盖主会话这组三组旧映射和重复迁移的幂等性。

### hsctl 的 fish 自动补全

在仓库根目录执行，给当前 fish 会话加载补全（支持 `hsctl` 和 `./scripts/hsctl`）：

```fish
source scripts/completions/hsctl.fish
```

永久启用：

```fish
mkdir -p ~/.config/fish/completions
cp scripts/completions/hsctl.fish ~/.config/fish/completions/hsctl.fish
```

输入 `./scripts/hsctl ` 后按 Tab 可补全命令；`monitor`、`canvas`、`size`、
`mode`、`overview`、`marks` 会提供对应参数。`-i` / `--instance` 后按 Tab
会通过 `hyprctl instances -j` 列出当前实例（需要 jq）。标记名称由用户输入。

### 临时 canvas 生命周期回归

```bash
python3 scripts/repro-canvas-ephemeral.py
```

在独立双屏实例检查：空临时 canvas 不连续扩展、编号不能跳过空页、离开与取消
overview 回收临时页面、任意成员屏幕的新窗口或移入窗口让 canvas 转正。测试也
覆盖浮动窗口及最后一个窗口关闭后正式 canvas 的保留；结束时关闭测试实例。
