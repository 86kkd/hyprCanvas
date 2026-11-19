# Feature: Topology Neighbor Index（拓扑邻居索引）

指令向功能计划。目标：**简化 scroller 的 `move_focus` / `move_window` 寻路**，不新增第二种布局。

配套清理清单：[`planned-cleanups.md`](./planned-cleanups.md)  
运行时地图：[`architecture-cn.md`](./architecture-cn.md)

## 1. 功能定义

实现一个 **只读派生** 的拓扑邻居索引，供方向路由统一查询。

| 项 | 要求 |
|---|---|
| 布局真相 | 仍是 lane / stack；索引不拥有窗口、不写几何 |
| 坐标 | `laneIndex` / `stackIndex` / `windowIndex` + mode 轴映射 |
| 查询 API | `neighbor(focus, direction) -> LocalTarget \| AdjacentLane \| CreateEphemeral \| CrossMonitor \| NoOp` |
| 调用方 | `move_focus` 与 `move_window` **必须共用**同一决策出口 |
| 禁止 | 使用 `GridItem.column/row/span` 或把 `GridModel` 接进寻路 |

代码标记：`TODO(topology-neighbor-index)`

## 2. 先决条件（按序完成）

做索引实现前，先完成这些清理（细节在 planned-cleanups）：

1. 按 planned-cleanups 瘦身公开命令：删 `focus_lane`、`create_lane`、`admit_window`、`expel_window`、`align_window`、`fit_size` 公共命令；`cycle_size` 换成 `set_window_size`
2. 删除 `scrollergrid`（`TODO(remove-scrollergrid)`）：取消注册、删除 `src/layout/grid/`、清掉 dispatch 并列分支与 grid 测试/脚本
3. 确认仓库内不再有「第二种正式布局」文档表述

未完成 1–2 前，不要大拆 handoff 实现。

## 3. 实现步骤

### 3.1 收敛路由决策为纯函数

1. 以 [`src/layout/canvas/route_logic.cpp`](../src/layout/canvas/route_logic.cpp) 为起点，或新增 `src/layout/canvas/topology_route.*`
2. 把「当前焦点 + 方向 + mode」映射成上一节的路由结果枚举
3. 单元测试覆盖：stack 内、stack 间、lane 间、ephemeral 空 lane、跨屏、NoOp

### 3.2 从 lane/stack 生成索引

1. 遍历 canvas 的 lane 链表与 stack/window 顺序，生成索引快照
2. 在成员关系变更、lane 增删、mode 切换后使索引失效并重建
3. 索引层只回答邻居与路由动作，不调用 compositor focus / 不改 `geom`

### 3.3 改接 `move_focus` / `move_window`

1. [`focus.cpp`](../src/layout/canvas/focus.cpp) 的 `CanvasLayout::move_focus`：先问索引，再执行对应动作
2. [`commands.cpp`](../src/layout/canvas/commands.cpp) 的 `move_window` 同级路径：复用同一 `neighbor` 决策，再做 payload 转移
3. 保留现有语义：ephemeral 空 lane、跨屏 handoff、fullsize/expanded 不并栈等策略仍生效，只换「下一步去哪」的表达

### 3.4 删除旧旁路

1. 去掉仅服务已删公开命令的路由分支
2. 去掉任何临时「投影到 grid 再寻路」的实验代码
3. 更新 architecture / README：寻路说明指向本 feature，不再介绍 scrollergrid

## 4. 验收标准

- [ ] `move_focus` 与 `move_window` 共用同一邻居/路由决策出口
- [ ] 布局真相仍是 lane/stack；无 `GridModel` 寻路依赖
- [ ] `scrollergrid` 已删除或不再可加载
- [ ] ephemeral 空 lane 与跨屏 handoff 行为可用 ctest 与/或 smoke 覆盖
- [ ] `ctest --test-dir ./Debug --output-on-failure` 全绿

## 5. 禁止事项

- 不要把 scroller「迁移成」格子布局产品
- 不要维护 lane/stack ↔ grid cells 双向同步
- 不要为了寻路简单而引入第二套布局真相
- 不要在未完成公开命令瘦身与删除 scrollergrid 前，把整棵 handoff 树重写到不可回滚

## 6. 工作顺序（执行清单）

```text
1. planned-cleanups：公开命令瘦身
2. TODO(remove-scrollergrid)：删除第二种布局
3. TODO(topology-neighbor-index)：实现本 feature（§3）
4. 文档与测试收尾（§4）
```
