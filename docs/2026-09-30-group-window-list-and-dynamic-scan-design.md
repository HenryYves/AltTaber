# AltTaber 新增：Alt+` 进程内窗口列表 + 切换期间动态扫描

> 日期：2026-09-30
> 状态：**已实现并交付**（HEAD `a7cf0c9`）。验收结果：
> Alt+`` ` `` 组列表（入口 A）与入口 B **经用户实测验收通过**；
> **动态扫描未验收** —— 用户明确不需要该功能，代码保留、两个开关默认关闭。
> 未决项与未验收范围见 `2026-09-30-group-window-list-and-dynamic-scan-known-issues.md`。
> 仓库：`repo/`（fork `HenryYves/AltTaber`）
> 新分支：从 `feat/configurable-blacklist` 的 HEAD `a665f50` 开（建议名 `feat/group-window-list-and-live-scan`）

---

## 0. 范围与前提

**做两件事**，都是新配置、**默认关闭**：

1. Alt+` 从"无界面盲切"变成"弹出玻璃背景的垂直列表"，列出该进程的窗口标题，按最后使用时间排序
2. 切换器显示期间以固定频率扫描，新出现的后台窗口动态入列，并可自动把选中项跳过去

**不做**：不拆 `widget.cpp` / `Util.cpp` 的整体文件结构（下次单独一轮，先拆后改）；不动上游 master 基线；不碰 `TaskbarWheelHooker` 的任务栏滚轮链路。

---

## 1. 已核实的事实（设计依据）

| 事实 | 依据 |
|---|---|
| Alt+` **已被 hook**，且单独按也能工作 | `KeyboardHooker.cpp:35-41` 拦下后 `postEvent` 直接投递给唯一静态 receiver（`KeyboardHooker.cpp:72`），绕过焦点 |
| 现有 Alt+` 是**盲切**：无界面，直接切组内下一个 | `widget.cpp:91-106` |
| 组内顺序数据已存在，无需新建结构 | `winActiveOrder[exePath][hwnd] = 时间戳`（`widget.cpp:225`），`sortGroupWindows()` 已按它排序（`widget.cpp:406-413`） |
| 主列表是**按进程分组**的（一进程一项） | `prepareWindowGroupList()`，`widget.cpp:233` |
| `listValidWindows()` 约 **2ms**（自注释实测），其中**已包含**通过廉价过滤窗口的进程路径查询 | `Util.cpp:323`；路径查询在 `isWindowAcceptable()` 内，`Util.cpp:286` |
| 图标有缓存，进程路径**无**缓存 | `getCachedIcon()` 静态 QHash，`Util.cpp:509`；`getWindowProcessPath()` 自注释 `// slow`，`Util.cpp:111` |
| 切换器此前**无法用 Esc 取消**，Alt+` 是唯一的"关掉"路径 | `widget.cpp:92-97` |

**代价结论**：全量重建列表做轮询**不可行**（重建会重置选中，且要吃 `widget.cpp:367` 注释里那个 472ms 的首行布局代价）；但**纯 HWND 集合 diff 的轮询可行**——每 tick 约 2ms，只有真出现新 HWND 时才为它单独查路径、取图标。

---

## 2. 功能一：Alt+` 进程内窗口玻璃列表

### 2.1 两条入口与各自的初始高亮

按 Alt+` 有**三种**情形，判据只看"当前 `Mode` + 是否可见"，互斥且穷尽：

| 情形 | 触发条件 | 行为 | 列表内容 | 初始高亮 |
|---|---|---|---|---|
| **A 独立触发** | `Widget` **不可见** | 进入 GroupMode 并显示列表 | 前台窗口所在进程的全部有效窗口 | **旧盲切的目标** |
| **B 从切换器进入** | `Mode == AppMode` **且可见** | 切到 GroupMode（窗口不重新 show，就地换内容） | **当前选中项**所在进程的全部有效窗口 | **第 0 行** |
| **C 已在组列表内** | `Mode == GroupMode` **且可见** | 高亮下移一行（见 §2.3） | — | — |

情形 A 与 B 都**不改变 `groupWindowOrder` 的既有语义**，只是把它的消费方从"立即切换"改成"填充列表"。

**入口 A 的高亮用算的，不用假设的**：定位前台窗口在列表中的下标 `i`，高亮 `(i+1) % N`；按住 Shift 时为 `(i-1+N) % N`。
不采用"当前窗口一定排首行，所以高亮第二行"这个捷径——前台窗口可能因故不在首行，那样高亮就偏了。用下标计算，高亮**精确等于**旧盲切的目标，兼容性是算出来的。

- 入口 A 的语义：**按一下松手 == 原来的盲切行为**，肌肉记忆不破。

**入口 B 的语义是"什么都不变的松手"**：`keyReleaseEvent` 本来就会切到 `getLastValidActiveGroupWindow(group.exePath)`（`widget.cpp:188-194`），而那正是列表第 0 行。所以入口 B 初值高亮第 0 行 ⇒ 按 ` 再松手，结果与不按 ` 直接松手**完全一致**。

> ⚠️ **本次会移除一个旧行为**：Alt+` 不再能"关掉"切换器（原 `widget.cpp:92-97` 的 `hide()` 分支被"进入 GroupMode"取代）。项目本来就不支持 Esc 取消，移除后 AppMode 期间没有键盘取消路径，只能靠松手提交或点击别处。
> **用户已确认接受此项**（2026-09-30）。

### 2.2 列表内容

- 该进程的**全部**有效窗口，**含当前窗口**，按最后使用时间**降序** ⇒ 当前窗口天然落在首行
- 每行**只有窗口标题文字**，不含图标
- 标题过长**截断**：右省略 `…`（`Qt::ElideRight`）
- 行宽按最长标题计算并 clamp 到 `[220, 480]` 逻辑像素
- 组内只有 1 个窗口时**照样显示**（就 1 行），不做特例——规则少一条，行为可预测

### 2.3 交互

| 操作 | 行为 |
|---|---|
| ` | 高亮下移一行（端点循环） |
| Shift+` | 高亮上移一行（端点循环） |
| Tab / Shift+Tab | 同 ` / Shift+`（GroupMode 内不再切 app） |
| ↑ ↓ ← → | 同 ` / Shift+` 映射为上下移 |
| Vim H J K L | 映射为上下移 |
| 鼠标滚轮 | 移动高亮 |
| 松开 Alt | **提交**：切到高亮窗口 → 回 AppMode → 隐藏 |
| 窗口失焦且鼠标不在其上 | 隐藏（沿用既有机制，`widget.cpp:61-69`） |

### 2.4 几何与外观

同一 HWND，所以毛玻璃（`setWindowBlur`）、圆角（`Util::setWindowRoundCorner`）、`forceShow()` 防闪烁（`widget.cpp:129-135`）**全部原样复用，零新增 HWND**。

GroupMode 下的尺寸单独算：行高固定；高度 = 行数 × 行高，超过 **8 行**封顶并内部滚动；宽度按 §2.2 的 clamp 规则。位置仍走现成的"按 `DisplayMonitor` 定屏 → 居中"那段（`widget.cpp:304-330`）。

---

## 3. 功能二：切换期间的动态扫描

### 3.1 组件 `WindowWatcher`

- 持一个 `QTimer`，**只在列表可见期间运行**（`requestShow` / `requestShowGroup` 启动，hide 时停止）——不常驻后台
- 每 tick 调一次 `listValidWindows()` 比对 HWND 集合，约 2ms
- diff 核心是**纯函数** `diffWindows(known, current) -> {added, removed}`，不依赖 Qt GUI，是整个改动里唯一能脱离界面单测的部分
- **只在 diff 非空时**发信号，避免空转
- **严格增量**：只为新出现的 HWND 查 `getWindowProcessPath` 和取图标，**绝不整表重建**

### 3.2 扫描间隔

**300ms**，写成具名常量。依据：每 tick 约 2ms ⇒ 占空比 < 1%，且远低于"注意到新窗口出现"的感知阈值。不暴露成配置项（YAGNI），将来要调再说。

### 3.3 入列与移除规则

| 场景 | 行为 |
|---|---|
| AppMode：新**进程** | 新增图标项。位置沿用现有排序语义（无使用记录 → 末尾；`new_app_second` 开 → 第 2 位），**不引入新规则** |
| AppMode：已有进程的新窗口 | **不**新增列表项，只更新该组的窗口列表（影响切过去时的候选） |
| GroupMode：本进程的新窗口 | 插入对应行，位置按最后使用时间排序 |
| 窗口被关闭 | **对称移除**。若移除的正是选中项，选中顺延到同下标（钳位到有效范围） |

### 3.4 自动选中（开关 B）

- 开关 B 打开时，**每次** diff 出新增都跳一次选中
- **手动优先**：本次弹出期间一旦发生**手动导航**（Tab / ` / 方向键 / Vim / 滚轮任一），置 `userNavigated = true`，**本轮不再抢**选中；列表下次弹出时重置
- AppMode 下**只对"新进程项"跳转**；已有进程多出来的新窗口不跳（列表项没变）

---

## 4. 组件与文件清单

| 文件 | 新增/改 | 内容 |
|---|---|---|
| `header/utils/WindowWatcher.h` | 新增 | 声明：QTimer、`diffWindows()` 纯函数、`newWindows`/`windowsGone` 信号 |
| `src/utils/WindowWatcher.cpp` | 新增 | 轮询与 diff 实现 |
| `header/utils/GroupWindowList.h` | 新增 | 封装竖排 `QListWidget`：填充行、维护选中、暴露当前 HWND |
| `src/utils/GroupWindowList.cpp` | 新增 | 上述实现 + **从 `widget.cpp` 挪入**的 `rotateWindowInGroup` / `rotateNormalWindowInGroup` / `buildGroupWindowOrder` / `sortGroupWindows` / `getLastActiveGroupWindow`（这几个本次本来就要改，挪走是零额外风险） |
| `header/widget.h` / `src/widget.cpp` | 改 | 加 `Mode{AppMode, GroupMode}`；入口 `requestShowGroup()`；按键/滚轮/label/提交按模式路由 |
| `header/utils/ConfigManager.h` | 改 | 加 `getWatchNewWindows()` / `getAutoSelectNew()`；**并给 `getBlacklistFileNames()` 加缓存**（见 §5） |
| `ui/widget.ui` | 改 | 加竖排 `QListWidget`（默认隐藏） |
| `CMakeLists.txt` | 改 | 登记两个新源文件 |
| `repo/CONFIG.md` + `repo/CONFIG_zh.md` | 改 | 新增 `[dynamic]` 节 |
| 父目录 `CONFIG.md` + `CONFIG_zh.md` | **删** | 与 `repo/` 下的完全重复（已 `diff` 验证逐字节相同），合并为 `repo/` 一份 |

### 4.1 搬函数时必须守住的约束

`Widget::groupWindowOrder` 这个成员**不止**给 Alt+` 用：任务栏滚轮链路 `rotateTaskbarWindowInGroup()`（`widget.cpp:476`）也在读它、写它，而 `clearGroupWindowOrder()` 被 `src/main.cpp` 的信号连接直接调用。搬动 §4 表格里那几个函数时：

- 保持 `groupWindowOrder` 的状态语义和清空时机不变（Alt 抬起清空见 `widget.cpp:182`）
- `rotateTaskbarWindowInGroup()` 本次**留在 `Widget`**，只改成调用搬到 `GroupWindowList` 的静态/共享辅助函数
- 搬完立刻编译 + 验一遍任务栏滚轮，防止"搬家"静默改坏这条独立链路

---

## 5. 配置

```ini
[dynamic]
watch_new_windows=true   ; 开关 A：切换期间扫描新窗口并动态入列。默认 false
auto_select_new=true     ; 开关 B：新窗口入列时自动选中它。默认 false；开关 A 关闭时此项无效果
```

同步更新 `CONFIG.md` / `CONFIG_zh.md`。

**顺带的必要优化**：`isWindowAcceptable()` 每个窗口都调 `cfg.getBlacklistFileNames()`（`Util.cpp:290`），而该函数每次调用都重新读 QSettings、再把逗号串 `split` + `trimmed` + `toLower` 一遍（`ConfigManager.h:53-63`）。静态扫描时这是"每窗口一次"，轮询后会变成每秒 N×3 次重复解析。本次给它加一层缓存，`ConfigManager::configEdited` 信号到达时失效。

---

## 6. 验证

**如实说明：本仓库没有任何测试框架。**

| 层次 | 手段 |
|---|---|
| 编译链接 | `rebuild.bat` 通过，无新增警告 |
| 纯函数 | `diffWindows()` 无 Qt GUI 依赖，将**变异测试**：把它的增/删判定各改错一次，确认能观测到差异 |
| 回归 | 现有 Alt+Tab / 任务栏滚轮 / 黑名单 / `new_app_second` 行为不变（人工核对） |
| 交互 | **由用户执行**：按住 Alt 连按 ` 看高亮走位、Shift 反向、入口 B 从切换器进入、后台起一个新窗口看它入列、松手看是否切对、开关四种组合各验一次 |

交互验证做成逐条可打勾的清单，交用户在真实桌面上跑。**不做"跑过了"式的含糊汇报。**

---

## 7. 已知风险与未决项

1. **`TaskbarWheelHooker.cpp:15` 有 `// this line may bomb`**（已知会崩的点）。与本次无关，但同属列表显示路径邻近区域，改动时避开。
2. **`winActiveOrder` 无界增长**（`widget.cpp:224` 的 TODO）。本次不修，但记录在案：动态扫描期间会频繁触发 `notifyForegroundChanged`，不加剧这个既有问题，也不缓解。
3. **`CMakeLists.txt:8` 硬编码了本机 Qt 路径** `D:/a_my/tool/Qt/6.8.1/msvc2022_64/lib/cmake`。**不改**。既然不往上提 PR（见第 4 条），它不构成"被打回"的问题，只是纯粹的可移植性债务：将来若要在别的机器上构建，需改为 configure 时 `cmake -DCMAKE_PREFIX_PATH=...` 传入。
4. **不往上提交 PR**。上游 `MrBeanCpp/AltTaber` **已无人维护**，改动只需合并回自己的 fork `HenryYves/AltTaber`。因此：
   - 分支从 `feat/configurable-blacklist` 的 HEAD 开、新提交里带着旧的未合并黑名单提交 —— **不是问题**，不需要为 PR 观感调整基底
   - 不需要为了"PR 干净"去拆提交或等旧 PR 先合
5. **单窗口时仍弹 1 行列表**（§2.2 的决定）。实现后若觉得吵，再单独加特例。
6. **`repo/CONFIG.md` 与父目录副本的删除**需用户点头后再执行（已确认内容逐字节相同）。
