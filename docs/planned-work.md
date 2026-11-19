# Planned Work

集中记录当前已在代码中标注、但尚未落地的计划项：公开 API 清理、功能落地（如拓扑邻居索引）、以及目标模型对齐（如 Canvas Workspace）。按 `TODO(...)` 检索与执行。

代码里统一用这些标记检索：

```text
TODO(set_window_size)
TODO(remove-align-window)
TODO(remove-fit-size-command)
TODO(remove-focus-lane)
TODO(remove-create-lane-command)
TODO(remove-admit-expel-window)
TODO(remove-scrollergrid)
TODO(topology-neighbor-index)
TODO(canvas-workspace-model)
```

相关决策背景也可对照：

- [`architecture-cn.md`](./architecture-cn.md) / [`architecture.md`](./architecture.md)
- [`topology-neighbor-index.md`](./topology-neighbor-index.md)（Feature：拓扑邻居索引；先瘦身并删 scrollergrid）
- 下文 [§9](#9-todocanvas-workspace-model目标所有权层级canvas-workspace)（Canvas Workspace 目标模型）
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
- [ ] 测试/脚本清理见 [§6](#6-测试脚本与文档清理计划)

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
- [ ] 测试/脚本清理见 [§6](#6-测试脚本与文档清理计划)

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
- [ ] 测试/脚本清理见 [§6](#6-测试脚本与文档清理计划)

---

## 4. `TODO(remove-focus-lane)` / `TODO(remove-create-lane-command)`：删除遗留 lane 命令

### 背景

这两个公开 API **不是自动化测试依赖**（`tests/` 无引用）。仅
[`hyprland-testing.md`](./hyprland-testing.md) 有手工 `hyprctl eval` 示例。

| 命令 | 实际行为 | 为何可删 |
|---|---|---|
| `focus_lane` | 只换 active lane（可 wrap） | `move_focus` 的相邻 lane 路由已覆盖，且更完整 |
| `create_lane` | 抽出 active stack，放进新建持久 lane | 日常由 `move_focus`（临时空 lane）和 `move_window` 覆盖 |

注意：`move_focus` 边界创建的是 **ephemeral 空 lane**，与公开 `create_lane` 的“带 stack 拆持久 lane”不同；但后者作为对外命令已无必要保留。

### 删除范围

- Lua：`create_lane` / `focus_lane`
- dispatch：`dispatch_createlane` / `dispatch_focuslane`
- Canvas：`CanvasLayout::create_lane` / `focus_lane`
- Grid：`GridLayout::focus_lane`；`GridLayout::create_lane` 需先改写
  `expel_window_right`（当前直接调用 `create_lane`），再删除

### 主要落点

| 区域 | 文件 |
|---|---|
| Lua / dispatch | `src/plugin/dispatch/layout_commands.cpp` |
| Canvas | `src/layout/canvas/layout.h`, `src/layout/canvas/commands.cpp` |
| Grid | `src/layout/grid/layout.h`, `src/layout/grid/layout.cpp` |
| 文档 | `README.md`, `docs/hyprland-testing.md` |

### 建议完成判定

- [x] 公共 Lua/dispatch/Canvas `focus_lane` 已删除
- [ ] 公共 Lua/dispatch/Canvas `create_lane` 已删除
- [ ] `GridLayout::expel_window_right` 不再依赖 `create_lane`
- [ ] `GridLayout::focus_lane` / `create_lane` 已删除或仅保留真正需要的内部实现
- [ ] README 与 hyprland-testing 示例已去掉这两条命令
- [ ] 测试/脚本清理见 [§6](#6-测试脚本与文档清理计划)

---

## 5. `TODO(remove-admit-expel-window)`：删除 `admit_window` / `expel_window`

### 目标

删除公开的 `admit_window` / `expel_window`。日常并栈 / 拆栈由 `move_window` 覆盖：

| 意图 | 旧命令 | `move_window` 替代 |
|---|---|---|
| 单窗并入相邻 stack | `admit_window` | stack 轴方向的 `move_window`（未到边缘时） |
| 多窗拆出新 stack | `expel_window`（仅右侧） | 多窗时 stack 轴 `move_window`（左/右均可拆） |

### 明确放弃的行为

`admit_window` 独有：多窗 stack 里仍把活动窗并进**前一个已有 stack**。  
落地删除后不再保留该手感；需要时只能先拆再移，或接受只用 `move_window`。

### 删除范围

- Lua：`admit_window` / `expel_window`
- dispatch：`dispatch_admitwindow` / `dispatch_expelwindow`
- Canvas：`admit_window_left` / `expel_window_right`
- Lane：`admit_window_left` / `expel_window_right`
- Grid：`admit_window_left`（已是 `move_window` 包装）、`expel_window_right`（仍走 `create_lane`，与 §4 一起清）

### 暂留

- `Stack::admit_window(...)`：内部 membership helper，仍被 `move_window` → `move_active_window_to_adjacent_stack` 使用，**不要**和公开命令一起删

### 主要落点

| 区域 | 文件 |
|---|---|
| Lua / dispatch | `src/plugin/dispatch/layout_commands.cpp` |
| Canvas | `src/layout/canvas/layout.h`, `src/layout/canvas/commands.cpp` |
| Lane | `src/layout/lane/lane.h`, `src/layout/lane/actions.cpp` |
| Grid | `src/layout/grid/layout.h`, `src/layout/grid/layout.cpp` |
| Stack（保留） | `src/model/stack/stack.h`, `src/model/stack/membership.cpp` |
| 文档 | `README.md` Admit/Expel 章节与绑定示例 |

### 建议完成判定

- [ ] 公共 admit/expel Lua/命令/Canvas/Lane/Grid 入口已删除
- [ ] `Stack::admit_window` 仍保留给 `move_window` 内部路径
- [ ] README Admit/Expel 说明与绑定示例已删除或改写为 `move_window`
- [ ] 测试/脚本清理见 [§6](#6-测试脚本与文档清理计划)

---

## 6. 测试、脚本与文档清理计划

清理命令时，测试侧分三层：**ctest 自动化**、**手工 repro 脚本**、**文档清单**。  
公开命令大多不被 ctest 直接调用；真正要改测试的是内部 helper / grid 模型，以及手工脚本里的旧 dispatcher。

### 6.1 自动化测试 `tests/` 现状

| 计划项 | ctest 是否引用公开命令 | 是否引用相关内部 API | 清理动作 |
|---|---|---|---|
| `focus_lane` | 否 | 否 | 无测试改动 |
| `create_lane` | 否 | 否 | 无测试改动 |
| `admit_window` / `expel_window` | 否 | 否（无公开命令引用） | 无测试改动 |
| `align_window` 公开命令 | 否 | 否（无 `align_window`/`align_stack`） | 无 scroller 侧测试改动 |
| `cycle_size` 公开命令 | 否 | 否 | 无 scroller 侧测试改动 |
| `fit_size` 公开命令 | 否 | **有 helper** | 见下 |
| `scrollergrid` / `GridModel` | **有** [`tests/grid_logic_tests.cpp`](../tests/grid_logic_tests.cpp) | 整模块 | 随 `TODO(remove-scrollergrid)` 删除测试 |
| grid resize/align 模型 | 否（非 Lua） | **有**（同上） | 随 scrollergrid 删除，不再单独迁移 |

#### 必须处理的测试

1. [`tests/core_logic_tests.cpp`](../tests/core_logic_tests.cpp)
   - 覆盖：`parse_fit_size_arg`、`select_fit_size_range`、`normalize_fit_size_range`
   - 对应：`src/core/fit_size.h`
   - 计划：
     - 只删公共 `fit_size` 命令时：**保留**这些 helper 测试
     - 若后续连 helper / `FitSize` 枚举也删掉：删除或改写 `test_fit_size_helpers` 及相关 `parse_fit_size_arg` 断言

2. [`tests/grid_logic_tests.cpp`](../tests/grid_logic_tests.cpp)
   - 覆盖：`test_resize_align_and_page_move`
     - `model.resize_active_item(...)`（尺寸循环语义）
     - `model.align_active(...)`（对齐语义）
   - 计划：
     - 落地 `set_window_size` / 删除 cycle 档位时：改写 resize 断言，使其覆盖 `n` 可见格/可见 stack 语义，而不是 next/prev 档位
     - 删除 align 语义时：删掉或改写 `align_active` 相关断言，只保留仍存在的 page move 行为

#### 当前无需改动的测试（就这些公开命令而言）

- `tests/layout_logic_tests.cpp`
- `tests/layout_snapshot_tests.cpp`
- `tests/overview_*_tests.cpp`
- 其他未引用上述 API 的逻辑测试
- `admit_window` / `expel_window`：**ctest 无引用**，删除公开命令时无需改自动化测试

### 6.2 手工 repro 脚本（非 ctest，但删命令后会坏）

| 文件 | 用到的旧 API | 清理动作 |
|---|---|---|
| [`scripts/repro-overview-accept-lane.sh`](../scripts/repro-overview-accept-lane.sh) | `scroller:createlane` | 改为 `move_focus` / `move_window` 等价路径，或删除该步骤 |
| [`scripts/repro-layout-persistence.sh`](../scripts/repro-layout-persistence.sh) | `scroller:createlane` | 同上 |
| [`scripts/repro-grid-real-coverage.sh`](../scripts/repro-grid-real-coverage.sh) | `scrollergrid` / `fitsize` 等 | 随删除 scrollergrid **整脚本删除或改写为 scroller 场景** |

说明：这些脚本里不少仍是旧 `dispatch scroller:...` 写法；清理时一并迁到 `hl.plugin.scroller.*`，避免 0.57 上假绿/假红。  
目前未发现 repro 脚本直接调用 `admitwindow` / `expelwindow`；删除时仍应全库再搜一遍。

### 6.3 文档清单

| 文件 | 内容 | 清理动作 |
|---|---|---|
| [`docs/smoke-test-checklist.md`](./smoke-test-checklist.md) | `cyclesize` / `fitsize visible`；grid fullscreen/fitsize | 改成新命令或删除对应手工项 |
| [`docs/hyprland-testing.md`](./hyprland-testing.md) | `create_lane` / `focus_lane` eval 示例；fitsize 说明 | 删除或改写示例 |
| [`README.md`](../README.md) | Lua commands 表、Resizing/Aligning/Fitting/Admit-Expel 章节、旧 bind 示例 | 随各项删除同步更新 |

### 6.4 建议完成判定（测试侧）

- [ ] `ctest --test-dir ./Debug --output-on-failure` 全绿
- [ ] `core_logic_tests` 中 fit_size helper 测试已按 helper 去留更新
- [ ] `grid_logic_tests` 已随 scrollergrid 删除（或确认不再构建）
- [ ] 相关 repro 脚本不再依赖 createlane / fitsize / scrollergrid
- [ ] smoke-test / hyprland-testing / README 不再推荐已删命令（含 admit/expel / scrollergrid）

---

## 7. `TODO(remove-scrollergrid)`：删除第二种布局算法

### 目标

删除 `scrollergrid`。它不是寻路简化方案，而是平行的格子布局产品；与「只改 move_focus/move_window 表达、保留 lane/stack」的目标不符。

功能计划见 [`topology-neighbor-index.md`](./topology-neighbor-index.md)。

### 删除范围

- `PLUGIN_INIT` 中 `HyprlandAPI::addTiledAlgo(..., "scrollergrid", ...)`
- [`src/layout/grid/`](../src/layout/grid/)（`GridLayout` / `GridModel` 等）
- dispatch 里对 `GridLayout` 的并列分支（`workspaceGridLayoutForAction` 等）
- overview / snapshot / persist 中仅服务 grid 的路径（若有）
- [`tests/grid_logic_tests.cpp`](../tests/grid_logic_tests.cpp)
- [`scripts/repro-grid-real-coverage.sh`](../scripts/repro-grid-real-coverage.sh) 及文档中的 scrollergrid 说明

### 建议完成判定

- [x] 无法再 `layout = scrollergrid` 加载该算法
- [x] 仓库无 `ScrollerGrid::` / `src/layout/grid` 生产路径
- [x] ctest 不再编译/运行 grid 专用测试
- [x] README / architecture / smoke-test 不再把 scrollergrid 写成并存布局

> 已完成：旧快照文件中的 `GRID` / `GRIDITEM` 行仍可解析但会被忽略，lane 状态照常恢复；
> `repro-grid-*.sh` 三个脚本已删除。

---

## 8. `TODO(topology-neighbor-index)`：用拓扑邻居索引简化寻路

完整指令向功能计划见 [`topology-neighbor-index.md`](./topology-neighbor-index.md)。

摘要：在 **保留 lane/stack** 的前提下，把 `move_focus` / `move_window` 的「下一步去哪」收成只读派生索引 + 纯函数路由；删除 scrollergrid，不引入格子布局。

### 建议完成判定

- [ ] 按 feature 文档 §3–§4 完成实现与验收
- [ ] `move_focus` 与 `move_window` 共用同一邻居/路由决策出口
- [ ] 不再依赖 `GridModel` / `scrollergrid`

---

## 9. `TODO(canvas-workspace-model)`：目标所有权层级（Canvas Workspace）

### 目标模型

在 **不替换** Hyprland workspace 的前提下，把 hyprscroller 的跨屏场景正式抽象为：

```text
Canvas Workspace          ← hyprscroller 抽象：一整套多屏桌面
  └─ Hyprland workspace × N(= monitor 数)   ← 每屏一块真实桌面
        └─ Lane → Stack → window
```

含义：

- **Canvas Workspace**：插件侧「场景」——一次切换应对齐整套多屏桌面；只做索引与切换策略，**不是**窗口归属的权威来源。
- **Hyprland workspace × N**：每个 monitor 挂且仅挂一个 member workspace；可见性 / 焦点 / `window→m_workspace` 仍由合成器决定。
- **Lane → Stack → window**：继续挂在**单个** Hyprland workspace 对应的 `CanvasLayout` 上；单屏排窗与寻路不跨过合成器 workspace。

### 与现状的关系

| 概念 | 现状 | 目标角色 |
|---|---|---|
| `CanvasWorkspaceRepository` / overview canvas | 已用 `{monitorId, workspaceId}` members 分组，并有 `make_members_exclusive` | 升格为正式 **Canvas Workspace** 模型，而不只是 overview 旁路 |
| `CanvasLayout` | 每个 Hyprland workspace 一个 tiled 实例，拥有 Lane 列表 | **保持**：单屏布局器；命名上勿与「Canvas Workspace」混称 |
| `TODO(topology-neighbor-index)` | 单 workspace 内简化 `move_focus` / `move_window` | 仍在 **Lane/Stack** 层；跨屏切场景走 Canvas Workspace |

### 必须守住的约束

- **N = 当前 monitor 数** 是运行时不变量；插拔屏要重建或补齐 members。
- **普通 member workspace 互斥**：同一编号 workspace 不得同时属于两个 Canvas Workspace（沿用 / 强化 `make_members_exclusive`）。
- **special workspace 单独规则**：可「存在但不显示」，不得与普通 member 对等默认绑进每个 canvas。
- **禁止** `Canvas Workspace → Lane`（跳过 Hyprland workspace）；合成器不认该归属。
- **禁止** 把 Canvas Workspace 当成窗口权威归属；单屏命令仍落到「当前 monitor 上的那个 member workspace」。

### 建议落地范围（文档优先，代码随后）

1. 在架构文档中固定上述层级与命名（区分 Canvas Workspace vs `CanvasLayout`）。
2. 梳理 overview / `ensureCurrentVisibleCanvas` / 跨屏 handoff，使「切 Canvas Workspace」语义与 member×monitor 对齐。
3. 需要公开切换 API 时，再单独加命令；本 TODO 先锁定模型，不强制本轮加 Lua。

### 建议完成判定

- [ ] `architecture.md` / `architecture-cn.md` 写明 Canvas Workspace → Hyprland workspace×N → Lane → Stack
- [ ] 代码与文档中「Canvas Workspace」与 `CanvasLayout` 角色不再混用
- [ ] Canvas Workspace members 与 monitor 数 / 互斥约束有明确实现或测试覆盖
- [ ] special workspace 不误当作普通 member 默认并入每个 canvas
- [ ] 单屏 `move_focus` / `move_window` 仍以当前 member workspace 的 `CanvasLayout` 为入口（可与拓扑邻居索引共存）

---

## 推荐处理顺序

1. **`remove-focus-lane`** — 无内部生产依赖、无 ctest 引用，可先整条删  
2. **`remove-admit-expel-window`** — 无 ctest 引用；保留 `Stack::admit_window`  
3. **`remove-align-window`** / **`remove-create-lane-command`** / **`remove-fit-size-command`** — 继续瘦身公开命令  
4. **`set_window_size` + 删除 `cycle_size`** — 尺寸语义替换  
5. **`remove-scrollergrid`** — 删除第二种布局及 grid 测试/脚本（可与命令瘦身并行，但勿再往 grid 加功能）  
6. **`topology-neighbor-index`** — 在 scroller 上落地邻居索引，简化 `move_focus` / `move_window`  
7. **`canvas-workspace-model`** — 文档与 overview/canvas 仓库对齐目标层级（可与 6 并行规划；改跨屏切换语义时再动代码）

## 验证建议

每项落地后至少：

```sh
cmake --build ./Debug -j
ctest --test-dir ./Debug --output-on-failure
```

若动到真实布局命令行为，再补 [`smoke-test-checklist.md`](./smoke-test-checklist.md) 中相关手动场景，并抽跑受影响的 `scripts/repro-*.sh`。
