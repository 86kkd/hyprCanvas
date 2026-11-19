# 架构指南

本文档面向新人，用来回答一个核心问题：

Hyprland 的事件或快捷键，是如何最终驱动 hyprscroller 移动窗口的？

英文版见 [`architecture.md`](./architecture.md)。

相关决策：

- 计划工作清单：[`planned-work.md`](./planned-work.md)
- Feature：拓扑邻居索引：[`topology-neighbor-index.md`](./topology-neighbor-index.md)

## 运行时一览

整个项目大致可以理解成一条流水线：

1. Hyprland 通过 [`src/plugin/entry.cpp`](../src/plugin/entry.cpp) 中的 `PLUGIN_INIT` 加载插件。
2. `PLUGIN_INIT` 负责注册：
   - `plugin:scroller:*` 配置项
   - `hl.plugin.scroller.*` Lua 命令（例如 `move_focus`）
   - 名为 `scroller` 的平铺布局算法 `CanvasLayout`
   - overview 渲染钩子，以及焦点/卡片指针辅助逻辑
3. 之后 Hyprland 会调用其中一类入口：
   - 用户按下快捷键时，走 [`src/plugin/dispatch/`](../src/plugin/dispatch/) 中的 Lua 命令
   - 窗口创建、移除、移动或重新布局时，走 `CanvasLayout` 的平铺算法回调
4. `CanvasLayout` 把请求路由到当前的 `Lane`。
5. 每个 `Lane` 再把操作路由到一个或多个 `Stack`。
6. `Stack` 更新窗口归属、当前选中项与几何布局。

如果只想走最短阅读路径，按下面顺序看文件：

1. [`src/plugin/entry.cpp`](../src/plugin/entry.cpp)
2. [`src/plugin/dispatch/registration.cpp`](../src/plugin/dispatch/registration.cpp) 与
   [`src/plugin/dispatch/layout_commands.cpp`](../src/plugin/dispatch/layout_commands.cpp)
3. [`src/layout/canvas/layout.h`](../src/layout/canvas/layout.h)
4. [`src/layout/canvas/core.cpp`](../src/layout/canvas/core.cpp)
5. [`src/layout/canvas/focus.cpp`](../src/layout/canvas/focus.cpp)
6. [`src/layout/canvas/commands.cpp`](../src/layout/canvas/commands.cpp)
7. [`src/layout/lane/core.cpp`](../src/layout/lane/core.cpp)、
   [`src/layout/lane/actions.cpp`](../src/layout/lane/actions.cpp)、
   [`src/layout/lane/geometry.cpp`](../src/layout/lane/geometry.cpp)
8. [`src/model/stack/core.cpp`](../src/model/stack/core.cpp)、
   [`src/model/stack/membership.cpp`](../src/model/stack/membership.cpp)、
   [`src/model/stack/geometry.cpp`](../src/model/stack/geometry.cpp)

## 核心概念

默认 `scroller` 布局主要有四类运行时对象。

### Plugin

Plugin 是面向 Hyprland 的外壳，主要代码在
[`src/plugin/entry.cpp`](../src/plugin/entry.cpp)。

它负责：

- 初始化日志，以及 layout / canvas 仓库
- 注册配置项
- 通过 `dispatchers::addLuaFunctions()` 注册 Lua 命令
- 注册 `CanvasLayout` 为 `scroller` 平铺算法
- 初始化 overview 渲染钩子、焦点指针同步，以及卡片指针命中测试

它本身不包含布局策略。

### CanvasLayout

`CanvasLayout` 是单个工作区 / canvas 的顶层控制器。
对外定义见 [`src/layout/canvas/layout.h`](../src/layout/canvas/layout.h)。

可以把它理解成 Hyprland 与插件内部布局模型之间的桥梁。

`CanvasLayout` 负责：

- 拥有一个工作区内有序的 `Lane` 列表
- 把 Hyprland 回调翻译成内部模型操作
- 对外暴露面向命令的方法，例如 `move_focus`、`move_window`
- 协调跨 lane、跨显示器交接
- 让缓存与当前 active lane 状态和 Hyprland 焦点保持同步

### Lane

`Lane` 是下一层。它在一个 canvas 内管理一条有序的 stack 条带。

在 row 模式下，lane 表现得像你可以横向滚动浏览的序列。
在 column 模式下，lane 仍然拥有 stacks，但 active stack 语义和移动规则会变化，
使纵向堆叠成为本地主导动作。

“像 PaperWM”的感觉主要从这里开始：lane 决定哪个 stack 处于活动状态、
相邻哪个 stack 应接收被移动的窗口，以及 stack 几何如何适配可见 canvas。

### Stack

`Stack` 拥有一组有序窗口，以及附着在这组本地窗口上的状态。

它负责：

- 栈内哪个窗口处于活动状态
- 添加、移除、恢复窗口 payload
- 该局部组的尺寸策略
- 其所拥有窗口的几何计算

如果说 `Lane` 回答的是“哪个 stack 应该处理这次操作？”，
那 `Stack` 回答的是“这个 stack 里的窗口具体会发生什么？”

### Window

真正的 Hyprland 窗口对象仍然来自 Hyprland。Hyprscroller 只做轻量簿记，
合成器才是真实客户端的权威来源。

插件只注册 `scroller` 一种平铺算法。原先的 `scrollergrid` 格子布局已删除；
方向寻路仍建立在 lane/stack 上（见 [`topology-neighbor-index.md`](./topology-neighbor-index.md)）。

## 主流程

### 启动

启动从 [`src/plugin/entry.cpp`](../src/plugin/entry.cpp) 开始。

先读 `PLUGIN_INIT`。它按顺序做这些事：

1. 设置日志
2. 初始化 layout / canvas 仓库
3. 注册配置项
4. 注册 `hl.plugin.scroller.*` Lua 命令
5. 注册 `scroller` 平铺算法
6. 初始化 overview 渲染钩子，以及焦点 / 卡片指针辅助逻辑

配置必须先于平铺算法注册。原因是：算法一旦挂上，Hyprland 就可能立刻创建布局对象，
而这些对象可能已经要读插件配置。

之后，Hyprland 就可以通过 Lua 命令调用或平铺布局回调进入插件。

### 新建平铺窗口

这是最容易跟踪的生命周期：

1. Hyprland 创建一个 tiled target。
2. 调用 `CanvasLayout::newTarget` 或 `CanvasLayout::movedTarget`。
3. 这些函数转发到 `CanvasLayout::onWindowCreatedTiling`。
4. `onWindowCreatedTiling` 查找或创建当前 active lane。
5. lane 按当前模式，把新窗口插入其 active stack。
6. 更新归属缓存，让后续可以从窗口直接查到 lane。

主要实现位于
[`src/layout/canvas/core.cpp`](../src/layout/canvas/core.cpp)。

### 焦点移动

方向性焦点从 Lua 命令开始：

1. 快捷键调用 `hl.plugin.scroller.move_focus("l")` 之类。
2. [`src/plugin/dispatch/layout_commands.cpp`](../src/plugin/dispatch/layout_commands.cpp)
   解析方向，并解析出当前活动的 `CanvasLayout`。
3. `CanvasLayout::move_focus` 生成 `TopologyIndex`（焦点所在的 lane / stack /
   window 坐标），再问 `CanvasLayoutInternal::neighbor` 这个方向通往哪里：
   - 当前 lane 内的本地目标
   - 相邻 lane
   - 在画布边缘新建临时空 lane
   - 跨显示器交接
   - 无动作
4. 激活选定的 lane 或目标显示器，并更新合成器焦点。

主要实现位于
[`src/layout/canvas/focus.cpp`](../src/layout/canvas/focus.cpp)。

### 窗口移动

方向性窗口移动流程类似：

1. 快捷键调用 `hl.plugin.scroller.move_window(...)`。
2. Lua 命令解析出当前活动的 `CanvasLayout`。
3. `CanvasLayout::move_window` 以“移动窗口”意图调用同一个 `neighbor` 查询，决定窗口：
   - 调整 stack 顺序、并入相邻 stack，或拆成新 stack
   - 进入相邻 lane，或进入画布边缘新建的 lane
   - 换到另一台显示器
4. 通过 payload 提取与插入辅助函数移动活动窗口，并尽可能保留源 stack / lane 状态。

主要实现位于
[`src/layout/canvas/commands.cpp`](../src/layout/canvas/commands.cpp) 与
[`src/layout/lane/actions.cpp`](../src/layout/lane/actions.cpp)。

### Overview

Overview 是一层独立的只读导航层。

逻辑拆分如下：

- overview session / model / scene / render 代码在 [`src/overview`](../src/overview)
- snapshot 构建发生在 `CanvasLayout` 内部
- overview 专用测试在 [`tests/`](../tests/)

最重要的一点：overview 打开期间不会改写真实的 lane / stack 几何。
它导航的是一份逻辑快照。

## 源码树如何划分

### `src/plugin/entry.cpp`

插件 ABI 入口与一次性注册。

### `src/plugin/dispatch`

把 `hl.plugin.scroller.*` 的参数解析并路由到强类型布局方法。

- `registration.cpp`：调用两组注册函数的引导入口
- `layout_commands.cpp`：布局命令，例如 `move_focus`、`move_window`
- `overview_mark_commands.cpp`：overview 与 marks 命令
- `shared.h`：解析当前活动工作区 / 布局的共享辅助

### `src/layout/canvas`

`scroller` 的工作区级控制平面。

- `core.cpp`：target 回调、工作区归属、lane 缓存、生命周期
- `focus.cpp`：焦点路由与跨显示器焦点交接
- `commands.cpp`：面向命令的变更操作，例如 movewindow
- `topology_route.cpp`：只读拓扑索引与 `move_focus` / `move_window` 共用的
  `neighbor` 决策；每次方向命令都从 lane 列表重新生成索引，不会过期
- `dispatch_logic.cpp` 与 `route_logic.cpp`：纯逻辑辅助，让决策密集代码可单测

### `src/layout/lane`

Lane 级行为。

- `core.cpp`：lane 归属与 active stack 簿记
- `actions.cpp`：stack / 窗口移动与转移动作
- `geometry.cpp`：lane 本地几何计算

### `src/model/stack`

本地 stack 数据结构。

- `core.cpp`：stack 构造与控制流辅助
- `membership.cpp`：添加 / 移除 / 恢复 / 提取窗口归属逻辑
- `geometry.cpp`：尺寸、适配与放置行为

### `src/overview`

Overview 的 session、model、navigation、scene 与 render 钩子。

### `src/core`

共享几何辅助、快照、方向解析、marks 存储，以及 Hyprland 运行时适配。

## 测试

测试目标不会启动 Hyprland。它关注的是那些在重构中仍应保持稳定的纯逻辑。

- [`tests/core_logic_tests.cpp`](../tests/core_logic_tests.cpp)
  覆盖共享辅助与底层逻辑。
- [`tests/layout_logic_tests.cpp`](../tests/layout_logic_tests.cpp)
  覆盖 dispatch 与交接决策辅助。
- [`tests/topology_route_tests.cpp`](../tests/topology_route_tests.cpp)
  覆盖焦点移动与窗口移动的方向邻居路由。
- [`tests/layout_snapshot_tests.cpp`](../tests/layout_snapshot_tests.cpp)
  覆盖布局快照行为。
- overview 相关测试拆在
  [`tests/overview_logic_tests.cpp`](../tests/overview_logic_tests.cpp)、
  [`tests/overview_model_tests.cpp`](../tests/overview_model_tests.cpp)、
  [`tests/overview_scene_tests.cpp`](../tests/overview_scene_tests.cpp)、
  [`tests/overview_session_logic_tests.cpp`](../tests/overview_session_logic_tests.cpp)、
  [`tests/overview_session_effects_tests.cpp`](../tests/overview_session_effects_tests.cpp)。

当你真正改动 `CanvasLayout`、`Lane` 或 `Stack` 的有状态行为时，
自动化测试是必要但不充分的。请同时配合手动
[冒烟测试清单](./smoke-test-checklist.md)；必要时再看
[Hyprland 测试说明](./hyprland-testing.md)。
若改动需要嵌套复现脚本或分阶段调试，也请遵循
[功能 / 调试工作流](./feature-debug-workflow.md)。

## 建议阅读策略

如果你刚接触这个项目，按这个顺序来：

1. 先通读本文，暂时不要打开代码。
2. 打开 [`src/plugin/entry.cpp`](../src/plugin/entry.cpp)，找到 `PLUGIN_INIT`。
3. 打开 [`src/plugin/dispatch/layout_commands.cpp`](../src/plugin/dispatch/layout_commands.cpp)，
   跟踪一条命令，例如 `dispatch_movefocus` / `lua_move_focus`。
4. 打开 [`src/layout/canvas/layout.h`](../src/layout/canvas/layout.h)，阅读 `CanvasLayout` 的公开方法。
5. 端到端跟踪一条生命周期：
   - 新窗口：`newTarget -> onWindowCreatedTiling`
   - 焦点：`hl.plugin.scroller.move_focus -> dispatch_movefocus -> move_focus`
   - 移动窗口：`hl.plugin.scroller.move_window -> dispatch_movewindow -> move_window`
6. 然后再深入 lane 与 stack 代码。

这个顺序能保持自上而下的心智模型。如果先从 `Stack` 开始，局部代码通常能看懂，
但整体运行时图景往往建立不起来。

## Hyprland 0.57 说明

Hyprland 0.57 不再注册插件 dispatcher。快捷键必须调用 `hl.plugin.scroller`
下的 Lua 函数，而不是旧的 `scroller:movefocus` 风格。
`plugin:scroller:focus_wrap` 这类插件配置项仍然通过 `addConfigValueV2` 单独注册。
