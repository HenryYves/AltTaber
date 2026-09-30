# 交互验证清单：组窗口列表 + 动态扫描（Task 6 / 7 / 8 / 9 / 10）

> 2026-09-30 | 分支 `feat/group-window-list-and-live-scan` | 被测代码 = `08d0ffd`（最后一个**代码**提交）
> 对象：`D:\a_my\project\workspaces_AND_tool-agnostic-projects\Daily_Claude\project\2026-06-16_alttaber_research\repo\build\Release\AltTaber.exe`（刚全量重编 + `windeployqt` 过的那个，编译于 2026-09-30 13:50）
> **这一栏只随「代码提交」重盖**：`08d0ffd` 之后只动文档（`repo/docs/`）的提交不改变二进制，不用为它们
> 重盖此栏 —— 否则每个文档提交都会让本清单的版本声明失效，逼出一次没有意义的复验。
> 共 **29 条**（R 组 4、A 组 8、B 组 5、C 组 6、D 组 6），每条一个 `- [ ]`，可打勾
> 修订 1（2026-09-30，复审后）：A1 / A2 / B2 / C2 / C3 / C4 / C6 / D1 / D2 改成**可执行判据**，
> 前置第 3 条改成无歧义的顺序，删掉一个已失效的引用。
> 修订 2（2026-09-30，分支终审后的修复轮）：删掉第九节原来的"第 2 条"——它声称删除高亮**之上**的行会让高亮
> "停在同一个行号、指到另一个窗口"，**实测不成立**（Qt 6.8.1 保留的是当前 *item*：删掉上方的行只会让行号下移，
> 高亮跟着**窗口**走）；C 组 / D 组的时延判据由"约 0.3 秒"放宽为"约 0.3–0.7 秒"（有效轮询周期从未被精确测过）。
> 修订 3（2026-09-30，同上修复轮）：表头按「钉代码提交、不钉 HEAD」重盖（`8be939e` → `08d0ffd`，编译时间
> 13:25 → 13:50），对象路径改成绝对路径（文档已移入 `repo/docs/`，`repo/build/...` 的相对写法不再有唯一解读）；
> **新增 D3**（鼠标点选组列表后开新窗口，高亮不得被抢走），D 组 5 → 6 条、总计 28 → 29 条，
> 原 D3 / D4 / D5 顺延为 D4 / D5 / D6（第九节与坑 1 里的引用同步改过）。

这份清单把 Task 6 ~ Task 10 各任务里所有标了「交用户跑」的条目合并成一份，去掉了重复与已失效的条目。
本仓的自动化测试只有一个纯逻辑单测（`WindowDiffTests.exe`，18 条，只覆盖 `WindowDiffTracker`），
**GUI 行为没有任何自动化覆盖**，所以下面每一条都只能由你在真实桌面上跑。
**控制端不会替你勾任何一条。**

---

## 一、本清单**不含**哪些（不要找它们）

Task 3 / Task 4 / Task 5 的交互条目**已经由你验证通过**（13/13，见
`2026-09-30-verification-T3-T4-T5.md`），本清单**不再重复**，具体是：

- Task 3 Step 4（黑名单缓存失效 + 阳性对照，2 条）—— 见那份清单的 B1–B6
- Task 4 的「默认关闭、行为不变」（2 条）—— 见 C1–C2
- Task 5 Step 8（`WindowActivationOrder` 纯搬家回归，5 条）—— 见 A1–A5

但是 **Task 6 Step 6 的那次回归不在上面那份清单里**（Task 6 发生在 Task 5 验收之后），
所以它是本清单的 **R 组**。

三处合并/作废说明，避免你按原计划数条目时对不上：

1. Task 9 Step 8 第 6 条「既有功能未坏：黑名单仍过滤 / `new_app_second` 不变 / Alt+`` ` `` 仍正常」
   与 Task 6 Step 6 的回归项重合，已合并进 **R 组**（其中「Alt+`` ` `` 仍正常」由 **A 组**整体覆盖）。
2. Task 7 Step 10 的第 9 条「用 Task 2 的临时脚手架观察，按下 Alt+`` ` `` 不应有 `[watch]` 刷屏」
   **已作废**：Task 9 Step 6 把那段临时脚手架连同所有 `[watch]` 打印一起删掉了。
   带阳性对照核实过：`grep -rn "\[watch\]" src/ header/` = **0 命中**，而 `grep -rn "watch" src/ header/` = **21 命中**
   （证明 grep 在该目录确实有效），`grep -rn "脚手架" src/` = **0** 而同串在 `.superpowers/sdd/` 下 10 个文件命中。
   没有可观察对象，故 A 组是 8 条而不是 9 条。
3. Task 6 Step 6 的第 2 条「Alt+`` ` `` **盲切**：连按应在同组窗口间轮换」**已经不能按原样跑**：
   Task 7 把 Alt+`` ` `` 从"无界面盲切"改成了"弹出组列表"（提交 `9a8d9eb`，这是本轮有意的行为变更，不是回归）。
   与它等价、且**可执行**的检查是 **A1**（见下）。所以 R 组只有 4 条。

---

## 二、跑之前必读：三个已知的坑

### 坑 1（最重要）：最可能的失败点是**窗口尺寸重算**，不是 watcher 或 diff 逻辑

动态扫描往可见的弹窗里增删行时，会调用 `layoutForCurrentMode()`，它对一个**已经可见**的无边框半透明窗口
调 `setGeometry`（`src/widget.cpp:462`）。这是 Windows 窗口管理器的行为，**这个项目里没有任何人在离屏环境下能复现或否证它**；
原作者自己在 `src/widget.cpp:464-475` 的注释里就记录过「图标数量变化时 resize 不生效」的困扰。

所以：**C2（A 开 / B 关，新程序出现在列表里）和 D1（组列表里新窗口出现）是最可能失败的两条。**
如果你看到「新程序没进列表」「列表变形/错位/半透明没重绘」「行数变了但框子没变」——
**先怀疑尺寸重算这条路，不要先去怀疑 watcher 定时器或 diff 逻辑。**
排查入口就是 `layoutForCurrentMode()` 的 `setGeometry`（`src/widget.cpp:462`）+
`groupList_->preferredSize()`（`src/utils/GroupWindowList.cpp:82-90`）。
（同一机制影响本清单里所有"弹窗开着的时候行数变了"的条目：C2 / C3 / C4 / C5 / D1 / D2 / D3 / D4 / D6。）

### 坑 2：Alt 键**点按，不要长按**

`src/utils/KeyboardHooker.cpp:46` 的注释明确写着：**按住 Alt + 方向键超过约 1 秒会触发一次假的 Alt release**
（Alt + 其他键不受影响）。A 组的方向键验收项正好踩在这上面——长按会被系统判成松手，弹窗会提前提交。

**所以 A5 请点按**（按一下抬一下），不要按住方向键不放。

### 坑 3：编译与运行互斥（流程约束，不是功能问题）

AltTaber 运行时会锁住 `build/Release/AltTaber.exe`，此时再跑 `rebuild.bat` 会以
`LNK1104: 无法打开文件 AltTaber.exe` 失败。**跑验收前要启动它，跑完请退干净**，
否则下一次编译会失败。

---

## 三、前置

1. **启动**（Qt DLL 已就位，直接双击即可）：
   `D:\a_my\project\workspaces_AND_tool-agnostic-projects\Daily_Claude\project\2026-06-16_alttaber_research\repo\build\Release\AltTaber.exe`
2. **配置文件**：`repo\build\Release\config.ini`（与 exe 同目录；托盘的 **Settings** 菜单会用记事本打开它）。
   你现在的这一份是：

   ```ini
   DisplayMonitor=1
   [label]
   font_size=10
   font_family=方正清刻本悦宋简体
   
   [blacklist]
   file_names=KeyCastOW.exe,keyviz.exe,Cat Gatekeeper.exe
   
   [sorting]
   new_app_second=true
   ```

3. **`[dynamic]` 段的起点 = 两个键都是 `false`**（它们默认 `false` 且**默认不写盘**，所以"没有这一段"也等于关闭）：

   ```ini
   [dynamic]
   watch_new_windows=false
   auto_select_new=false
   ```

   **顺序不要跳**：

   - **R 组、A 组、B 组**：保持上面这个 `false` / `false` 的起点（也就等于 C1 的前置）。
   - **C 组**：按 **C1 → C2 → C3 → C4 → C5** 的顺序走，每换一行就改一次 `config.ini`；
     C6 用你当时手里的任一组合都行。**每改一次都要重启 AltTaber**（原计划的要求，不热加载）。
   - **D 组**：`watch_new_windows=true`，`auto_select_new=true`（D2 也用它——它靠"先按一下 `` ` `` "来让位，不是靠关掉这个开关）。
4. **回报方式**：逐条报「通过 / 不通过」（可以直接说"R1 通过""C2 不通过：新程序没出现"）。
   不通过就描述现象（"新程序没出现"、"框子还是原来的高度"、"高亮没动"……），
   每条后面都写了该回头查哪个 Task 的哪一步，我会照着定位。

---

## 四、R 组：老链路回归（Task 6 Step 6；合并了 Task 9 Step 8 第 6 条）

Task 6 只是加了新类和换了个头文件，**不应该有任何可观测变化**。下面几条分别指向不同的代码路径，坏哪条都能顺藤摸到。

- [x] **R1** 按 Alt+Tab 唤出，连按几下走位，松手
  - 预期：切到正确窗口，**排序与改动前一致**（最近用过的在前）
  - 失败时回查：Task 6 Step 2/3（`WindowInfo` 换文件）、`prepareWindowGroupList()` 排序；排序错还要看 Task 5 的 `WindowActivationOrder`

- [x] **R2** **在任务栏图标上滚动滚轮**
  - 预期：同组窗口前后切换；向上滚恢复、向下滚最小化
  - 失败时回查：Task 6（`rotateTaskbarWindowInGroup` 链路）；Task 5 搬 `winActiveOrder` 时动过这条链路的共享状态

- [x] **R3** 看 Alt+Tab 列表
  - 预期：`KeyCastOW.exe` / `keyviz.exe` / `Cat Gatekeeper.exe` **都不在**列表里
  - 失败时回查：Task 3 的黑名单缓存（T3/T4/T5 清单 B 组验过，此处只确认没被后续 Task 撞坏）

- [x] **R4** 启动一个从没切过的新程序，再按 Alt+Tab
  - 预期：它排在**第 2 位**（你的 `new_app_second=true`）
  - 失败时回查：Task 6（`WindowInfo` 头文件搬家）、Task 9 的插入排序

> R2 是这一组里最容易被后面几个 Task 撞坏的（Task 9 动过滚轮事件的分发），务必试。

---

## 五、A 组：入口 A —— 独立按 Alt+`` ` `` 弹出组列表（Task 7 Step 10）

- [x] **A1** 让一个有 3 个窗口的程序在前台（例如开 3 个窗口的记事本），按一下 Alt+`` ` `` **立即松手**
  - 预期：切到该组**上一次用过的那个窗口**，也就是 A2 里弹出列表时**高亮的第 2 行**那一项。
    **可执行判据**：先做 A2 并记住第 2 行是哪个窗口，再重复本步——落点应与它一致。
  - 为什么不能写"与改动前对比"：旧的"盲切"行为已被 `9a8d9eb` 移除，**任何构建都跑不出它了**，没人能做那个对比。
  - 失败时回查：Task 7 **Step 5**（`requestShowGroup()` 里 `initialRow = ((idx+delta)%n+n)%n`，`widget.cpp:732-739`）
    与 **Step 7**（`commitGroupSelection()` 的提交，`widget.cpp:754-759`）；Task 8 Step 1（入口 B 分支的进入条件）

- [x] **A2** 再按一次 Alt+`` ` ``，**按住不动**
  - 预期：弹出玻璃背景的竖排列表，标题按最近使用降序，当前窗口在第一行，**高亮在第二行**
  - 失败时回查：高亮值是 **Task 7 Step 5**（`requestShowGroup()`）**算**出来的：`(idx+1)%n`，`widget.cpp:732-739`
    （按住 Shift 时 `-1`）；**Task 7 Step 4**（`enterGroupMode()`）只是用 `:695` 的 `setCurrentRow(initialRow)` 把它设进去。
    **若按了却什么都不弹** ⇒ 看 Task 7 勘误 §(a) 的 `forceShow()` 守卫
    （`requestShowGroup()` 里 `if (!this->isVisible()) forceShow();`，`widget.cpp:748-749`）：
    入口 A 触发时窗口是隐藏的，少了它就是"设了几何但没人显示"。
  - 例外（不当失败算）：`design.md:51-52` 明确说"当前窗口一定排首行、所以高亮第二行"这个捷径是代码**刻意不走**的
    （前台窗口可能因故不在首行，那时高亮要跟着偏）。所以你若看到高亮落在别处，先判断"前台窗口是不是在首行"，再下结论。

- [x] **A3** 连按 `` ` `` 两下
  - 预期：高亮下移两行；到底后**循环**回第一行
  - 失败时回查：Task 7 勘误 §(b) 的按键路由 + `GroupWindowList::moveSelection()`（`GroupWindowList.cpp:49-54`，取模实现端点环绕）

- [x] **A4** 按 Shift+`` ` ``
  - 预期：高亮上移；到顶后循环到最后一行
  - 失败时回查：同 A3 的 `-1` 分支

- [x] **A5** 按住 Alt 时**点按** ↑ ↓ ← → 和 H J K L（**见坑 2，别长按**）
  - 预期：都是上下移动
  - 失败时回查：Task 7 勘误 §(b)：`keyPressEvent` 必须按 `mode_` 分流到 `groupList_->moveSelection()`。
    **若窗口被最小化了**，说明方向键又落进了 AppMode 分支（把滚轮投给了隐藏的 `lw`），这正是勘误 §(b) 要堵的那个缺陷

- [x] **A6** 松开 Alt
  - 预期：切到高亮的那个窗口，列表消失
  - 失败时回查：Task 7 Step 7（`commitGroupSelection()`：`hide()` 必须在 `switchToWindow` **之后**）

- [x] **A7** 找窗口标题超长的一个
  - 预期：标题**右省略**（`…`），没有横向滚动条
  - 失败时回查：`GroupWindowList.cpp:9-10`（`Qt::ElideRight` + 横向滚动条永远关闭）；宽度 clamp `MinWidth=220` / `MaxWidth=480`（`header/utils/GroupWindowList.h:15-16`）

- [x] **A8** 单窗口的程序按 Alt+`` ` ``
  - 预期：弹出一行的列表，松手无变化
  - 失败时回查：`GroupWindowList::preferredSize()` 的 `qBound(1, count, 8)`（`GroupWindowList.cpp:88`）

---

## 六、B 组：入口 B —— 从切换器里按 `` ` `` 进入组列表（Task 8 Step 3）

- [x] **B1** 按 Alt+Tab 唤出切换器，把高亮移到某个**不是前台**的应用（例如第 3 个图标），按 `` ` ``
  - 预期：弹出**那个应用**的竖排窗口列表（不是前台应用的）
  - 失败时回查：Task 8 Step 1（入口 B 分支取的是 `lw->currentItem()` 的 `exePath`，不是前台窗口；`widget.cpp:710-713`）

- [x] **B2** **不变式检查**：重复上一步，但按下 `` ` `` 后**立刻松手**
  - 预期：结果与「直接松手、不按 `` ` ``」**一样**（都切到那个应用最近使用的窗口 = 列表第 0 行）
  - **例外（已知边界，不算本条失败）**：若该组**最近使用的那个窗口此刻已经关闭/被过滤**，
    AppMode 的释放路径会去读一份**未排序的旧快照**（`widget.cpp:715-718` 的注释；那是 `widget.cpp:251`
    那条既有 `TODO 需要排序（lastActiveWindow 被关闭情况下）`）。代码注释明说这种情况下**本入口反而更准**。
    所以"不完全一样"只在这条窄路径上被允许，其余情形仍应完全一样。
  - 失败时回查：Task 8 Step 1 的初始高亮必须是**第 0 行**（`widget.cpp:720` 的 `enterGroupMode(exePath, 0)`）。
    若在"MRU 窗口还活着"的正常情形下切到了别的窗口，就是这条不变式真破了

- [x] **B3** 在组列表里连按 `` ` `` 再松手
  - 预期：切到更早使用的那个窗口（顺序与 A3 一致）
  - 失败时回查：Task 7 的按键路由（B 组和 A 组共用同一条 `moveSelection` 路径）

- [x] **B4** 高亮停在**第一个**图标上按 `` ` ``
  - 预期：也正常弹出该应用的列表
  - 失败时回查：Task 8 Step 1：`enterGroupMode()` 的返回值被检查（`if (!enterGroupMode(...)) return false;`，`widget.cpp:720`）

- [x] **B5** 在组列表状态下松开 Alt，然后再按一次 Alt+Tab
  - 预期：切窗口、列表消失，**下一次 Alt+Tab 正常回到 app 列表模式**（不是还停在组列表）
  - 失败时回查：Task 7 Step 8（`requestShow()` 里重置 `mode_ = Mode::AppMode`）+ Task 7 复审堵掉的「陈旧 GroupMode」

---

## 七、C 组：AppMode 动态扫描的四种开关组合（Task 9 Step 8）

**前置**：`[dynamic]` 段落按前置第 3 条的**顺序**走（C1 从"两个都是 `false`"起），
**每换一行都要改 `config.ini` 并重启 AltTaber**。
统一操作：按 Alt+Tab 唤出切换器（**不要松手**），然后另开一个**新程序**。
**时延判据（C2 / C3 / C4 都适用）**：新程序应在 **约 0.3–0.7 秒内**出现（轮询间隔 `WindowWatcher::IntervalMs = 300`，
`header/utils/WindowWatcher.h:18`）。**有效周期从未被精确测过**：唯一一次测量是 Task 2 记到的 **~680 ms**，
且当时构建里还留着一条逐 tick 的日志行，成因未查明、之后也没复测 —— 所以 0.5–0.7 秒**不算失败**。
明显滞后（例如 1 秒以上）⇒ 先查这个常量与定时器的启停（Task 9 Step 3）。

- [ ] **C1** `watch_new_windows=false` / `auto_select_new=false`（**起点**：`config.ini` 里没有 `[dynamic]` 段，或两个键都是 `false`）
  - 操作：唤出切换器，另开一个新程序
  - 预期：列表**不变化**，选中**不跳**
  - 失败时回查：若列表变了，说明默认值没生效或轮询没停：Task 9 **Step 3**（`requestShow()` / `hideEvent()` 里的启停）、Task 4 的 `getWatchNewWindows()`

- [ ] **C2** `watch_new_windows=true` / `auto_select_new=false`（**见坑 1**，这条最可能失败）
  - 操作：同上
  - 预期：新程序**出现在列表里**（约 0.3–0.7 秒内），选中**不动**
  - 失败时回查：先查尺寸重算（`widget.cpp:462` 的 `setGeometry`），再查插入路径 `widget.cpp:516` 起
    （`onNewWindows` 的 AppMode 分支）与 `widget.cpp:561-575` 的兜底去重。明显滞后 ⇒ `WindowWatcher::IntervalMs`。
    对应 Task 9 **Step 4**（AppMode 的入列）

- [ ] **C3** `watch_new_windows=true` / `auto_select_new=true`
  - 操作：同上，然后松手
  - 预期：新程序出现（约 0.3–0.7 秒内）**且选中跳到它**；松手切过去
  - 失败时回查：Task 9 **Step 4** 的自动选中（`widget.cpp:590` 读 `cfg.getAutoSelectNew() && !userNavigated_`）

- [ ] **C4** `watch_new_windows=true` / `auto_select_new=true`
  - 操作：唤出切换器 → **先按两下 Tab** → 再开一个新程序
  - 预期：新程序**入列**（约 0.3–0.7 秒内），但选中**不跳**（你手动翻过）
  - 失败时回查：Task 9 **Step 5**（手动导航打标记）的 `userNavigated_` 置位点（`widget.cpp:126/137/144/154/162/171`）。若仍跳了，就是手动优先没生效

- [ ] **C5** `watch_new_windows=true` / `auto_select_new` 任一
  - 操作：开着一个程序 → 唤出切换器 → **关掉它**
  - 预期：它**从列表消失**；若它正是当前选中，选中**顺延到相邻项**而不是越界
  - 失败时回查：Task 9 **Step 4** 的对称移除（`removeEmptyGroups()`，`widget.cpp:616-633`，返回值在 `:652` 被用作"项数变了才重算几何"的门）。越界由 `setCurrentRow()` 的 `qBound` 兜（`GroupWindowList.cpp:44-47`）

- [ ] **C6** 任一组合（就用你当前手上那份配置）
  - 操作：唤出切换器后**静置 10 秒**（Alt 按住不放），同时打开**任务管理器 → 详细信息 → AltTaber.exe** 看「CPU」列
  - 预期：列表不卡顿，且 AltTaber 的 CPU 占用基本贴着 **0%**（每个 tick 只是枚举一次窗口，不出现持续尖峰）
  - 失败时回查：Task 2 的 `WindowWatcher` / `WindowDiffTracker` 的轮询开销；Task 3 的黑名单缓存（`isWindowAcceptable()` 每个窗口都会调，缓存失效会让 tick 变贵）

---

## 八、D 组：GroupMode 动态扫描（Task 10 Step 4）

**前置**：`[dynamic]` 里 `watch_new_windows=true`、`auto_select_new=true`，重启 AltTaber。
用浏览器这类容易开新窗口的程序。
**时延判据（D1 / D2 / D3 适用）**：新窗口应在 **约 0.3–0.7 秒内**出现（`WindowWatcher::IntervalMs = 300`，
`header/utils/WindowWatcher.h:18`）。同样地，有效周期从未被精确测过（见 C 组那条判据的说明）：0.5–0.7 秒
不算失败；明显滞后 ⇒ 先查这个常量与 `enterGroupMode()` 里的启停（`widget.cpp:692-693`）。

- [ ] **D1** 多窗口程序里按 Alt+`` ` `` 弹出组列表，**不松手** → 用快捷键（如浏览器 Ctrl+N）开一个新窗口
  - 预期：新窗口**出现在列表里**（约 0.3–0.7 秒内）且**选中跳到它**，松手切过去
  - 失败时回查：**见坑 1**，先查尺寸重算；再查 `onNewWindows` 的 GroupMode 分支（`widget.cpp:522`，
    必须在任何 `mode_ != Mode::AppMode` 判定**之前**）、`enterGroupMode()` 里的启动轮询（`widget.cpp:692-693`）

- [ ] **D2** 同上，但弹出后**先按一下 `` ` ``** 手动翻过 → 再开新窗口
  - 预期：新窗口**入列**（约 0.3–0.7 秒内）但**选中不跳**
  - 失败时回查：Task 10 的 GroupMode 分支读 `userNavigated_`（`widget.cpp:534`）

- [ ] **D3** 最少 3 个窗口的程序里按 Alt+`` ` `` 弹出组列表，**不松手** → **用鼠标点一下另一行**（不是当前高亮的那行）→
  再用快捷键（如浏览器 Ctrl+N）开一个**该进程**的新窗口 → 松手
  - 预期：新窗口**入列**（约 0.3–0.7 秒内）但**高亮仍停在你点的那一行**；松手切到**那一行**的窗口，**不是**新出现的那个
  - 失败时回查：鼠标分支置位 `userNavigated_` 的地方（`widget.cpp:772`，它必须同时匹配组列表）与 GroupMode 插入
    路径读它的地方（`widget.cpp:534`）。高亮被新窗口抢走 ⇒ 鼠标分支漏了组列表

- [ ] **D4** 弹出组列表后**关掉其中一个窗口**
  - 预期：它**从列表消失**；若它正是选中项，选中**顺延而不越界**
  - 失败时回查：`onWindowsGone()` 的 GroupMode 分支（`widget.cpp:637-647`，必须在 `:649` 那个守卫之前）；`GroupWindowList::removeByHwnd()`

- [ ] **D5** 弹出组列表后开一个**别的程序**的新窗口
  - 预期：组列表**不受影响**（只收本进程的）
  - 失败时回查：`widget.cpp:526-527` 的 `currentGroupExePath_` 比对（大小写不敏感，`Qt::CaseInsensitive`）

- [ ] **D6** 行数增加超过 8 行
  - 预期：列表高度**封顶**、不撑破屏幕
  - 失败时回查：`MaxVisibleRows = 8`（`header/utils/GroupWindowList.h:17`，用它的 `qBound` 在 `GroupWindowList.cpp:88`）
    + `layoutForCurrentMode()` 重算几何。**注意**：GroupMode 下滚轮**不再滚动列表**，而是移动高亮
    （Task 9 复审确认；`widget.cpp:775-781`）——所以"看不到藏起来的行"不算失败，用方向键/滚轮把高亮移下去即可

---

## 九、已知但**不算**验收失败的观察（别把它们当成新回归）

这些是复审已经记录在案、**有意留到后续**的行为，你如果在下面几条里碰到，如实描述即可，不用当成阻塞项：

1. **本组窗口全部关掉后**，弹窗会留一个**空的单行框**直到松手（`preferredSize()` 的 `qBound(1, 0, 8)`）。
   松手什么都不会发生（安全的空操作）。D4 会碰到。
2. **D6 的自动滚动**：新行是程序化选中的，Qt 是否会自动把它滚进可见区，离屏环境**没能证实**
   （Task 10 报告 §7）。若你看到"选中项在可视区之外"，记下来即可，修法是 `setCurrentRow()` 之后补 `scrollToItem()`。
3. **B2 的那条例外**（`widget.cpp:715-718` 的未排序快照）与本条清单第九节第 1 条同属"已知边界"：
   会出现，但已被代码注释/复审记录在案，不算回归。
4. **`CONFIG.md` 两处措辞不够精确**（文档完整性问题，不是功能缺陷）：
   ① 文档说"让位持续到下次弹窗"，而代码在**同一个弹窗里按 `` ` `` 进入组列表**时也会重置 `userNavigated_`（`widget.cpp:675`）；
   ② 文档列举的手动操作是"Tab / `` ` `` / 方向键 / 滚轮"，**漏了鼠标点击**（`widget.cpp:772`）。

---

## 十、跑完之后

把你逐条的结果发回来，包含：通过 / 不通过、不通过的现象描述。
我会按每条「失败时回查」那个入口去定位是哪个 Task 的哪一步，再决定是补丁还是记为已知项。
**这 29 条在拿到你的结果之前，一律视为"未验证"**，不会在任何文档里写成"已验证"。

---

## 十一、验收结果（2026-09-30，用户报告）

| 组 | 内容 | 条数 | 结果 |
|---|---|---|---|
| **A** | Alt+`` ` `` 弹出组列表（入口 A） | 8 | **全部通过** |
| **B** | 从切换器按 `` ` `` 进组列表（入口 B） | 5 | **全部通过** |
| **C** | AppMode 动态扫描 | 6 | **未验收** |
| **D** | 组列表动态扫描 | 6 | **未验收** |

**C/D 未验收的原因**：用户明确表示用不上"切换期间动态扫描"这个需求，主动决定不做验收。
**不是失败、不是阻塞、不是忘了。** 代码保留，`[dynamic]` 两个开关默认关闭，日常使用不受影响。

**这意味什么**：本支线里"Alt+`` ` `` 组列表"那一半**有实测验收证据**；"动态扫描"那一半
（`WindowWatcher` 轮询、新窗口入列、自动选中、几何刷新）**只有静态复审与离屏探针证据，
没有任何人在真实桌面上跑过**。将来要启用它的人，请把它当作**未验收的新功能**。

风险最高的一项已点名：动态增删后对**已可见的无边框半透明窗口**调 `setGeometry()`——
见 `2026-09-30-group-window-list-and-dynamic-scan-known-issues.md` 第一节。

（第十节那句"一律视为未验证"到此为止：A/B 已由用户验收，C/D 由用户决定不验。）
