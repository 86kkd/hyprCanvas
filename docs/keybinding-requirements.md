# 快捷键需求与现实约束

本文整理用户已确认的快捷键需求、实际指法及当前方案，供后续调整配置时参考。当前映射以 `examples/canvas-layout.lua` 为依据；文档不代表对正在重构的插件重新完成了运行验证。

## 1. 目标与键位分区

使用 F75／75% 键盘，主要通过左手修饰键和右手方向键操作桌面。

- **H/J/K/L 表示单体窗口操作**：移动焦点、搬动窗口、交换窗口。
- **N/M/,/. 表示整体空间操作**：显示器与 canvas workspace 的导航，以及携带窗口跨越这些空间。
- 两排方向一致，保持左／下／上／右的顺序。
- workspace 是二维空间，需要四方向导航，不能简化为上一页／下一页。
- “整体”指操作的目标空间，不表示把整个 workspace 搬到另一个显示器。

| 方向 | 单体窗口区域 | 整体空间区域 |
| --- | --- | --- |
| 左 | H | N |
| 下 | J | M |
| 上 | K | `,` |
| 右 | L | `.` |

配置中的 `SUPER` 对应 Win，`comma` 对应逗号，`period` 对应句号。

## 2. 指法：以连续动作而非孤立组合为依据

### 用户已确认的左手习惯

- 移动焦点时，小指直接按 Win，动作最小，是常用起手姿势。
- Ctrl + Win：小指按 Ctrl，无名指按 Win，比较顺手。
- Win + Alt：小指按 Win，无名指按 Alt，比较顺手。
- Ctrl + Shift 也好按；具体指法以用户实际习惯为准。
- Win + Shift 已找到可用指法：小指抬高按 Shift，无名指放低按 Win。

**Win + Shift 能舒服地按出，不等于它适合作为移动焦点后的追加动作。**

如果起手是小指 Win，接着按 Win + Shift 就需要换指：小指离开 Win，无名指接管 Win。这比保持小指、无名指追加 Alt 的动作更大。

### 高频动作链

1. 小指按住 Win，右手选择窗口。
2. 小指不动，无名指追加 Alt，右手搬动窗口。
3. 松开 Alt，立即回到焦点导航。

因此，Win + Alt 优先分配给搬动窗口。不能仅因 Shift 常被理解为“移动”就把最高频搬动操作放到 Win + Shift。

右手可在方向操作时临时使用食指 H/N、中指 J/M、无名指 K/逗号、小指 L/句号；这是可选操作手位，不要求普通打字也采用这一指法。

### 设计原理：修饰键代数与优先级取舍

本方案围绕四条原则收敛出最终映射，避免“只看单个组合好按，整体语义却互相冲突”：

1. **命名空间隔离（全员带 `Win`）**
   - 所有 scroller 方向操作统一收进 `Win` 命名空间，明确区分“桌面控制（带 `Win`）”与“应用内快捷键（不带 `Win`）”。
   - 因此放弃原先占用 `Ctrl + Shift + H/J/K/L` 的 `swap_window`（会覆盖终端、浏览器与编辑器里的常用快捷键），改为 `Win + Shift + H/J/K/L`，并把 `Ctrl + Shift + H/J/K/L` 完整归还给应用。
2. **修饰键正交代数（一修饰键一语义）**
   - **`Win`**：第一层导航（`move_focus` / `switch_workspace`）。
   - **`+ Alt`**：永远表示在对应导航组合上**携带窗口搬运**（`Win` → `Win + Alt`、`Ctrl + Win` → `Ctrl + Win + Alt`），单窗、canvas 与显示器三层保持同一套肌肉和心智模型。
   - **`+ Ctrl`**：专用于下排 `N/M/,/.` 的**第二空间层（显示器）**，不再同时用于上排 `H/J/K/L` 的窗口交换，避免 `Ctrl + Win` 在不同排分别代表“导航”和“交换”。
   - **`Win + Shift`**：专用于上排 `H/J/K/L` 的**原地交换（`swap_window`）**，与导航/搬运逻辑解耦。
3. **连续动作优先（零换指主链路）**
   - 最高频的“导航 → 搬运 → 回到导航”全部落在 `Win` ↔ `Win + Alt` 上：小指始终锚定 `Win`，无名指按放 `Alt` 即可切换，无需换指。
   - `Win + Shift`（小指抬高按 `Shift`、无名指放低按 `Win`）作为独立起手舒适，但换指成本高于追加 `Alt`，因此只分配给频率低于搬动的 `swap_window`。
4. **主键两排约束下的空间优先级（Workspace 优先于 Monitor）**
   - 7 组方向动作包含 3 对“导航 + 搬运”（单窗、canvas workspace、显示器）与 1 组交换。在坚持右手不离开主键两排（`H/J/K/L` 与 `N/M/,/.`）且保持 `+ Alt` 搬运语义统一的前提下，受鸽巢约束必然有一对空间操作落入 `Ctrl + Win` / `Ctrl + Win + Alt`。
   - `move_focus` 与 `move_window` 在布局边界本身即可跨屏，而 canvas workspace 是真正的二维四方向空间且跨 canvas 搬窗没有替代方向热键；因此将高频的 **canvas workspace** 放在第一层（`Win` / `Win + Alt`），将低频直达的**物理显示器**下放至第二层（`Ctrl + Win` / `Ctrl + Win + Alt`），把三修饰键的使用频率降到最低。

### 仍存在的折中

- Swap 使用 `Win + Shift`（小指抬高按 Shift、无名指放低按 Win），作为独立操作可舒适按出，但相比 `Win + Alt` 仍需从小指 `Win` 换指；由于交换频率低于搬动，该折中可接受。
- 跨显示器搬窗口使用 `Ctrl + Win + Alt`，严格遵循“导航组合 `+ Alt` 搬运”的语义，但涉及三个修饰键；由于 `move_window` 到边界亦可跨屏，该三键仅用于低频直达跨屏，尚未作为高频动作验证长期舒适度。
- 快捷键顺手程度以用户实测为准，不用标准打字指法替代个人反馈。

## 3. 当前确认并采用的映射

以下方向键均按左／下／上／右排列。

| 操作 | 快捷键 | 插件接口 |
| --- | --- | --- |
| 窗口焦点 | Win + H/J/K/L | `move_focus` |
| 搬动窗口、调整布局 | Win + Alt + H/J/K/L | `move_window` |
| 交换窗口 | Win + Shift + H/J/K/L | `swap_window` |
| 四方向切换 workspace | Win + N/M/,/. | `switch_workspace` |
| 跨 workspace 搬窗口 | Win + Alt + N/M/,/. | `move_window_to_workspace` |
| 显示器焦点 | Ctrl + Win + N/M/,/. | `focus_monitor` |
| 直接跨显示器搬窗口 | Ctrl + Win + Alt + N/M/,/. | `move_window_to_monitor` |
| Overview 打开／确认退出 | Win + I | `toggle_overview` |
| 展开／恢复 stack | Win + F | `toggle_fullscreen` |

在 scroller 中，`Ctrl + Win + H/J/K/L`、`Ctrl + Win + Alt + H/J/K/L`、`Ctrl + Shift + H/J/K/L` 以及 `Win + Shift + N/M/,/.` 均已解绑。

## 4. 已确定、不再占用方向组合的功能

- Overview 固定使用 Win + I，再按一次确认当前选择并退出。这里的“关闭”不是放弃选择、恢复原窗口。
- 展开／恢复 stack 固定使用 Win + F。
- 横向／纵向布局由插件自动控制，不安排手动切换快捷键。
- 可见 stack 数通过 Lua 配置手动设置，不占用这套方向键组合。
- 不要求把所有修饰键组合用满；未使用的组合可以留空。

## 5. 插件语义和焦点约束

### 三种搬动不能混淆

- `move_window` 依据本地布局拓扑搬动，可能涉及重排、合并、拆分或到达边界后的跨屏行为。
- `move_window_to_monitor` 直接搬到相邻物理显示器，焦点跟随窗口。
- `move_window_to_workspace` 搬到相邻 canvas 中同一物理显示器对应的工作区，并跟随切换整个 canvas。

canvas workspace 包含多个显示器各自的原生 workspace。切换 canvas 应保持这组成员关系，不能简单用原生 workspace 编号加减代替。

### 焦点验收要求

- 搬动之后键盘焦点跟随原窗口，不应被源页其他窗口、目标页旧窗口或空窗口抢走。
- 除最终 activewindow 外，还要检查移动过程中的瞬时焦点事件，避免“先异常刷新，再恢复”的问题被最终状态掩盖。
- 搬走窗口后，源 workspace 应记住合理的剩余窗口；切回来恢复该选择，而不是无条件选择第一个窗口。
- 其他显示器的焦点记忆不能被改写。
- 必须覆盖源页最后一个窗口、空目标、新建 canvas、快速往返、鼠标跟随和 `no_warps` 场景。
- 延迟鼠标同步不能把焦点或指针拉回已离开的窗口。
- 跨 workspace 接口当前针对平铺窗口；浮动窗口、特殊 workspace、overview 中调用和非法方向应保持无操作。

## 6. 与现有桌面配置共存的约束

本方案只接管 scroller 模式的相关按键。用户所谓“默认快捷键”包括本机发行配置和用户自定义，并不全是 Hyprland 内置默认。

- 进入 scroller 时，先移除同组合的原有绑定，再安装插件动作，避免一键触发多个动作。
- 切回 master/dwindle 时，恢复原有用途，移除插件专用组合，不能留下吞键的空回调。
- 同步维护 `ChangeLayout.sh`：初始化、切换布局以及重复刷新都不能叠加绑定。
- 保留 Win + Y 布局切换等无关功能。
- 原桌面预设的 Win + Shift + N（通知面板）、Win + Shift + M（音乐入口）和 Win + Shift + K（快捷键搜索）用户不使用：scroller 中释放 Win + Shift + K 给 `swap_window`（切回 master/dwindle 时恢复），并解绑未使用的 Win + Shift + N/M/,/.。
- scroller 中解绑 Ctrl + Shift + H/J/K/L，使其完整归还给应用程序；切回 master/dwindle 时恢复原有的 Ctrl + Shift + H/L 跨工作区移动。
- scroller 中不应启用绕过 canvas 成员关系的原生编号窗口转移或整工作区跨显示器移动。
- Tab、数字和滚轮等已有 canvas 导航入口可以保留；这次分区不是要求删除所有其他导航方式。

## 7. 文件与版本管理

- 项目示例：`examples/canvas-layout.lua`。
- 本机生效配置：`~/.config/hypr/lua/canvas-layout.lua`。
- 本机加载入口：`~/.config/hypr/hyprland.lua`。
- 布局切换脚本：`~/.config/hypr/scripts/ChangeLayout.sh`；项目参考版本在 `examples/ChangeLayout.sh`。
- 模块已从 `scroller-workspaces.lua` 改名为 `canvas-layout.lua`，后续引用统一使用新名称。
- 本机配置由 `~/.config/hypr/.git` 管理；项目源码、示例、文档与测试由项目自身的 Git 仓库管理，不能把两个仓库混为一谈。
- 用户要求直接用 Git 管理修改，不额外生成备份文件。
- 项目提交遵循 `docs/commit-convention.md`；不能混入其他正在进行的改动。

## 8. 修改后的最小验证范围

1. Lua 语法检查和现有快捷键测试。
2. 模拟 scroller → master/dwindle → scroller，检查恢复行为与重复加载。
3. 查询实际会话绑定：目标组合各一份，旧组合已移除，无配置错误。
4. 若新增插件接口，先确认已加载的二进制支持它，再安装调用该接口的绑定。
5. 涉及窗口转移或焦点逻辑时，运行基础测试和独立 Hyprland 集成回归；仅文档修改不需要重跑图形测试。

已有验证入口包括 `tests/scroller_keybinds_tests.lua`、`scripts/repro-canvas-window-transfer.py` 和 `docs/hyprland-testing.md`。
