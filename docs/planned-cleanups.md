# Planned Cleanups

集中记录当前已在代码中标注、但尚未落地的清理项，方便以后按条目查询和解决。

代码里统一用这些标记检索：

```text
TODO(set_window_size)
TODO(remove-align-window)
TODO(remove-fit-size-command)
```

相关决策背景也可对照：

- [`architecture-cn.md`](./architecture-cn.md) / [`architecture.md`](./architecture.md)
- README 中的 Lua commands 表（清理后需同步删文档示例）

---

## 1. `TODO(set_window_size)`：用可见 stack 数替换 `cycle_size`

### 目标

- 新增 `set_window_size(n)`：按「当前显示器工作区同时显示几个 stack」设主轴尺寸  
  - 语义：`span ≈ workarea / n`
  - 普通屏常见 `n=2`，带鱼屏可设 `n=3` 或 `4`
- **彻底删除**旧 `cycle_size` 的 next/prev 档位逻辑（`OneThird` / `OneHalf` / `TwoThirds`）
- 避免旧循环档位继续影响 fullscreen / admit / expel 等路径

### 不做 / 注意

- 当前 `Lane::resize_active_stack` 只拦 `maximized()`，对 expanded/fullscreen 防护偏弱；新 API 应明确与这些状态的交互
- `plugin:scroller:column_default_width`（`onehalf` / `onethird` / …）需一并迁到「可见 stack 数」模型
- `scrollergrid` 目前共用 `cycle_size` 命令表面，需同步改或删除

### 主要落点

| 区域 | 文件 |
|---|---|
| Lua / dispatch | `src/plugin/dispatch/layout_commands.cpp` |
| Canvas API | `src/layout/canvas/layout.h`, `src/layout/canvas/commands.cpp` |
| Lane | `src/layout/lane/lane.h`, `src/layout/lane/actions.cpp` |
| Stack 预设 | `src/model/stack/stack.h`, `src/model/stack/core.cpp` |
| Grid | `src/layout/grid/layout.h`, `src/layout/grid/layout.cpp` |
| 文档 | `README.md`（`cycle_size` 说明与绑定示例） |

### 建议完成判定

- [ ] 已提供 `hl.plugin.scroller.set_window_size(n)`（或最终定名）
- [ ] `cycle_size` / `cycle_window_size` / `resize_active_stack(step)` 及 1/3·1/2·2/3 循环路径已删除
- [ ] 默认宽度配置与新语义对齐
- [ ] grid 侧不再残留旧 cycle API
- [ ] README / 架构文档中的旧命令说明已更新

---

## 2. `TODO(remove-align-window)`：删除手动对齐命令

### 目标

整条删除手动 `align_window` 工作流。自动滚动/焦点可见性已覆盖日常需求，不再保留 left/right/center 的 Lazy-align 旁路。

### 删除范围

- Lua：`hl.plugin.scroller.align_window`
- dispatch：`dispatch_alignwindow`
- Canvas / Grid：`align_window`
- Lane：`align_stack`
- Stack：`align_window`

### 主要落点

| 区域 | 文件 |
|---|---|
| Lua / dispatch | `src/plugin/dispatch/layout_commands.cpp` |
| Canvas | `src/layout/canvas/layout.h`, `src/layout/canvas/commands.cpp` |
| Lane | `src/layout/lane/lane.h`, `src/layout/lane/actions.cpp` |
| Stack | `src/model/stack/stack.h`, `src/model/stack/membership.cpp` |
| Grid | `src/layout/grid/layout.h`, `src/layout/grid/layout.cpp` |
| 文档 | `README.md` Aligning 章节与绑定示例 |

### 建议完成判定

- [ ] 对外命令与内部 align API 已全部删除
- [ ] 无残留 `Reorder::Lazy` 仅服务于 align 的死路径（若有，一并清理）
- [ ] 文档示例已去掉 `align_window` / `alignwindow`

---

## 3. `TODO(remove-fit-size-command)`：删除公共 `fit_size` 命令

### 目标

删除用户可调用的 `fit_size` 命令表面。它**不是** overview 的替代品：

| | 公共 `fit_size` | overview |
|---|---|---|
| 作用 | 真改 stack/窗几何，把选定范围塞进工作区 | 只读导航层 |
| 资源 | 主要是 CPU relayout，几乎不占额外显存 | GPU overlay / framebuffer / texture，更重 |
| 与空白预览 bug | 无关，留着也修不好 overview 渲染问题 | 空白窗应在 overview render 路径修 |

因此：**不要为 overview 渲染 bug 保留 `fit_size`。**

### 删除范围（公共命令）

- Lua：`hl.plugin.scroller.fit_size`
- dispatch：`dispatch_fitsize`
- Canvas / Grid / Lane 的命令向 `fit_size(...)` 入口

### 暂留 / 另议

内部仍有 `Stack::fit_size(FitSize::All, …)` 以及 `src/core/fit_size.h` 辅助，被 **admit / relayout** 等路径使用。  
删除公共命令时：

1. 先拆掉对外 API
2. 再决定内部 helper 是保留、内联，还是改写成更小的专用函数

### 主要落点

| 区域 | 文件 |
|---|---|
| Lua / dispatch | `src/plugin/dispatch/layout_commands.cpp` |
| Canvas | `src/layout/canvas/layout.h`, `src/layout/canvas/commands.cpp` |
| Lane | `src/layout/lane/lane.h`, `src/layout/lane/actions.cpp` |
| Core helper | `src/core/fit_size.h`（先标命令删除；helper 可后清） |
| Stack（内部） | `src/model/stack/stack.h`, `src/model/stack/geometry.cpp` |
| Grid | `src/layout/grid/layout.h`, `src/layout/grid/layout.cpp` |
| 文档 | `README.md` Fitting the Screen 章节 |

### 建议完成判定

- [ ] 公共 `fit_size` Lua/命令/Canvas/Lane/Grid 入口已删除
- [ ] admit/relayout 等内部调用已有明确去留方案
- [ ] README 中 fitsize 说明与 submap 示例已更新
- [ ] overview 空白预览问题单独跟踪，不与本项捆绑

---

## 推荐处理顺序

1. **`remove-align-window`** — 依赖最少，可先整条删干净  
2. **`remove-fit-size-command`** — 先删公共命令，内部 helper 第二步再收  
3. **`set_window_size` + 删除 `cycle_size`** — 涉及默认宽度、预设枚举与 grid，改动面最大，建议单独做

## 验证建议

每项落地后至少：

```sh
cmake --build ./Debug -j
ctest --test-dir ./Debug --output-on-failure
```

若动到真实布局命令行为，再补 [`smoke-test-checklist.md`](./smoke-test-checklist.md) 中相关手动场景。
