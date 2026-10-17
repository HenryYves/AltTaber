# AltTaber 多屏切换模式 · 组列表 DWM 缩略图 · 任务栏缩略图回退诊断 — 实现计划

> 日期：2026-10-17
> 仓库：`D:\a_my\project\Forks\AltTaber\repo`（fork `HenryYves/AltTaber`）
> 基线分支：`feat/group-window-list-and-live-scan`，HEAD `161fc08`
> 夜间提交分支：`overnight/2026-10-17-multimonitor-thumbnail`（**起飞前必须已存在**）
> 执行方式：夜间无人值守。GUI 步骤要求执行时屏幕未锁。
> 本轮不做：2026-09-30 known-issues 里「有意未修」的条目（用户 2026-10-17 判断影响不大）。

---

## 1. 范围

三件事：

1. **Feat1** 多屏切换行为，新增一个三选一的配置。现状是选项 0。
   - 选项 0（现状）：列表显示所有屏幕的应用，切过去后窗口留在它原来的屏幕。
   - 选项 1：列表只显示「当前屏幕」的应用。当前屏幕 = 弹窗渲染到的屏幕（默认配置下就是鼠标所在屏幕）。组内窗口也按同一块屏幕过滤。
   - 选项 2：列表仍显示全部应用；切换时如果目标窗口不在当前屏幕，就把它搬到当前屏幕再激活。
2. **Feat2** 新配置启用后，Alt+`` ` `` 组列表右侧显示一块 DWM 实时缩略图。**一张，跟随当前高亮行**；宽高可配置。
3. **Fix1** 诊断「点任务栏缩略图切过去之后，鼠标朝上离开任务栏又会切回原应用」。本轮只加诊断日志、出探针启动脚本与复现协议，**不改行为**（根因未定，不盲修）。

---

## 2. 已核实的事实（依据）

| 事实 | 依据 |
|---|---|
| 应用列表由 `Widget::prepareWindowGroupList()` 枚举全部有效窗口后按进程分组 | `src/widget.cpp:331-383` |
| 弹窗屏幕由 `Widget::targetScreen()` 决定：`DisplayMonitor=0` 用主屏，`1` 用光标下屏幕，并在本次弹出期间钉住 | `src/widget.cpp:448-461` |
| 所有窗口几何只在 `layoutForCurrentMode()` 里算，且只做居中、不兜宽度 | `src/widget.cpp:464-521` |
| 组列表窗口集来自 `orderedGroupWindows()` → `WindowActivationOrder::orderedWindowsOf()` | `src/widget.cpp:709-714` |
| AppMode 松手提交在 `keyReleaseEvent()`；GroupMode 提交在 `commitGroupSelection()` | `src/widget.cpp:281-300`、`810-815` |
| 任务栏滚轮切换走 `rotateTaskbarWindowInGroup()`，与本轮改动无关 | `src/widget.cpp:921-1058` |
| 任务栏钩子只在光标位于任务栏上时安装；离开任务栏时发 `leaveTaskbar`，接收方只清 `groupWindowOrder` | `src/utils/TaskbarWheelHooker.cpp:52-72`、`src/main.cpp:50-51` |
| Qt 6.8 可用 `QScreen::nativeInterface<QNativeInterface::QWindowsScreen>()->handle()` 取 `HMONITOR` | Qt 6.8 文档 `qnativeinterface-qwindowsscreen.html`（Context7 已核对） |
| DWM 缩略图的目标窗口必须是本进程拥有的顶层窗口；缩略图由 DWM 在合成时绘制在目标窗口内容与子控件之上 | Microsoft Docs / sdk-api（2026-10-17 检索） |
| **DWM 缩略图在 Qt 无边框半透明窗口上可用**（2026-10-17 实测） | 探针 `C:\Users\Yves\AppData\Local\Temp\alttaber_probe`：Qt 6.8.1、Win11 26300、150% 缩放；`DwmRegisterThumbnail` 返回 `S_OK`，目标矩形中心截屏像素 `220,30,30`（源窗口纯红），背景像素 `17,17,17` |
| **`fSourceClientAreaOnly=TRUE` 能去掉标题栏与边框**（2026-10-17 实测） | 同一探针改为 `DWM_TNP_SOURCECLIENTAREAONLY` + `fSourceClientAreaOnly=TRUE` 后，缩略图左上角像素 `220,30,30`（源客户区纯红），而不是标题栏颜色；结论：只取客户区，预览干净 |
| **圆角必须做在宿主的顶层窗口上**（2026-10-17 实测） | 探针 `dwm_round_probe`：缩略图目的地窗口设 `DWMWA_WINDOW_CORNER_PREFERENCE=DWMWCP_ROUND` 后，窗口四角像素透出背景色 `0,172,0`、内部仍是源色 `220,30,30`，说明 DWM 缩略图被窗口圆角裁掉；`SetWindowRgn(CreateRoundRectRgn)` 同样能裁。结论：缩略图宿主必须是独立顶层窗口，圆角加在宿主上；主弹窗是整窗圆角，无法只圆缩略图那一块 |
| **注册/更新/注销的调用开销可忽略**（2026-10-17 实测） | 探针 `dwm_bench`，200 次循环：`DwmRegisterThumbnail` 平均 66.7µs（中位 57.2，最大 433.4）；`DwmUpdateThumbnailProperties` 平均 65.8µs；`DwmUnregisterThumbnail` 平均 85.7µs。三者都在 0.1ms 量级，不会拖慢按键路径 |
| DWM 的 `rcDestination` 与 `GetClientRect` 都是**物理像素** | 同上探针：Qt 逻辑尺寸 620×420 在 150% 下为 930×630 物理；按物理坐标放置的缩略图位置正确 |
| 本机当前只接了一块显示器（1707×1065 逻辑，150% 缩放） | 2026-10-17 `System.Windows.Forms.Screen::AllScreens` 只返回 `\\.\DISPLAY6` |

---

## 3. 设计决策

### 3.1 Feat1：多屏切换模式

新增配置：

```ini
[monitor]
switch_mode=0
```

| 值 | 名称 | 行为 |
|---|---|---|
| `0` | AllKeepPosition | 现状：显示全部，窗口留在原屏幕 |
| `1` | CurrentOnly | 应用列表与组列表都只保留「窗口位于当前屏幕」的条目 |
| `2` | AllMoveToCurrent | 显示全部；提交切换时把目标窗口搬到当前屏幕 |

- 「当前屏幕」统一取 `targetScreen()` 的返回值，即**本次弹窗渲染的那块屏幕**。它由 `DisplayMonitor` 决定，默认跟随鼠标。
- `switch_mode=1` 时：
  - AppMode 分组阶段就按窗口的 monitor 过滤，组内不会混入其它屏幕的窗口。
  - GroupMode 的 `orderedGroupWindows()` 同样过滤；如果过滤后为空，`enterGroupMode()` 返回 false（不弹空列表）。
  - 动态扫描（`onNewWindows`）只接收当前屏幕的新窗口；窗口自己跨屏移动不追踪，下次弹出重新计算。
- `switch_mode=2` 时：
  - 只在 AppMode 松手提交与 GroupMode 提交两处搬窗；任务栏滚轮切换不搬（本轮范围外）。
  - 目标窗口已经在当前屏幕就不动。
  - 窗口尺寸保持不变，居中到当前屏幕工作区；窗口大于工作区时贴工作区左上角，不缩小。
  - 最小化窗口：移动它的「还原位置」，随后 `switchToWindow()` 会还原并激活。
  - 最大化窗口：先 `SW_RESTORE`，移动还原矩形，再 `SW_MAXIMIZE`。
  - 搬窗用 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` 取真实外框，失败时退回 `GetWindowRect`；工作区取 `GetMonitorInfo().rcWork`。

**关于「Windows 跨屏时不是会自动变大小吗」**：Windows 不会因为前台切换自动移动窗口，也不会替我们决定位置。跨屏时会发生的是 DPI 变化：目标窗口若是 per-monitor DPI aware，Windows 会发 `WM_DPICHANGED` 并给出建议矩形，应用自己按新 DPI 重排；非 per-monitor aware 的窗口由 DWM 位图拉伸。因此本轮采用「保持物理尺寸、居中、让 `WM_DPICHANGED` 走 Windows 原生路径」；如果真机跨不同缩放率的屏幕时观感不对，下一轮再按 `目标DPI/源DPI` 显式缩放尺寸。这个取舍写进文档，不在本轮临场改。

### 3.2 Feat2：组列表 DWM 缩略图

新增配置（沿用 `[group_list]`）：

```ini
[group_list]
thumbnail=true
thumbnail_width=320
thumbnail_height=180
```

- `thumbnail` 默认 `false`。关闭时布局与现在逐像素一致。
- 宽高是**上限盒**；实际预览按源窗口外框的宽高比等比缩放并居中放进这个盒子里。宽度钳位 `80..2000`，高度钳位 `60..2000`，非法值退回默认。
- 只作用于 GroupMode（Alt+`` ` `` 的组列表）。AppMode 不显示。
- 预览区放在组列表右侧，固定间距 12px；整个弹窗宽度随之增大，居中逻辑不变。
- **缩略图宿主是独立顶层窗口**：DWM 的窗口圆角只能作用于整个顶层窗口，主弹窗是整窗圆角，无法只圆缩略图那一块。因此新建一个 `Qt::Tool` 子顶层窗口（owner = 主弹窗、无边框、不接受焦点、输入穿透、置顶、半透明背景），尺寸正好等于预览区；DWM 缩略图注册到这个宿主上。
- 宿主圆角：首选 Win11 的 `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_ROUND)`；失败（Win10）回退 `SetWindowRgn(CreateRoundRectRgn(0, 0, w + 1, h + 1, 16, 16))`。两种方式都已用探针确认会裁掉缩略图四角。
- 宿主用 Win32 `SetWindowPos` 定位（物理像素），避免 Qt 顶层窗口跨屏 `setGeometry` 的已知问题；主弹窗隐藏、切回 AppMode、缩略图源不可用时一并隐藏宿主。
- **不用「在弹窗内画遮罩」**：DWM 缩略图由 DWM 在目标窗口内容与子控件之上合成，Qt 画的遮罩在它下面，盖不住；独立遮罩窗口则要复刻弹窗的半透明与模糊背景，四个角会露馅。宿主的窗口圆角/窗口区域本身就是遮罩，采用它。
- **性能策略：切换优先，缩略图延迟**。高亮移动、列表绘制立即发生；缩略图更新走 60ms 合并定时器：每次高亮变化重置定时器，只处理最后一次；弹窗隐藏、切回 AppMode、源窗口变化时取消待处理更新。源 hwnd 没变就不重复注册。组列表先出现，缩略图随后填入，不阻塞按键路径。
- 源窗口 = 当前高亮行对应的 hwnd。高亮变化、动态增删、布局变化时更新；窗口已最小化或被 cloaked 时隐藏预览（DWM 对这类源窗口没有可靠内容）。
- **只取源窗口客户区**：`DWM_THUMBNAIL_PROPERTIES` 带 `DWM_TNP_SOURCECLIENTAREAONLY` 且 `fSourceClientAreaOnly=TRUE`，去掉标题栏与外框，预览更干净。等比缩放用的源尺寸取 `GetClientRect(source)`（不是 `DwmGetWindowAttribute` 的外框，也不是整窗尺寸），因为客户区的高宽比与整窗不同。
- 注意：应用自绘的标题栏（Electron、UWP、部分 Qt 应用）画在客户区里，仍会出现在预览中；这是「客户区」的边界，不是 DWM 的边框。
- 注册失败（`DwmRegisterThumbnail` 失败）只记一条 warning，功能静默降级为不显示，不影响切换。
- DWM 坐标是物理像素：Qt 逻辑矩形乘 `devicePixelRatioF()` 后再写进 `DWM_THUMBNAIL_PROPERTIES.rcDestination`。

### 3.3 Fix1：只诊断，不修

2026-10-17 代码阅读结论：本程序的鼠标钩子只在光标位于任务栏上时安装，且只处理 `WM_MOUSEWHEEL`；离开任务栏只发 `leaveTaskbar` → `clearGroupWindowOrder()`，这条路径没有任何切窗动作。因此「点缩略图后上移切回原应用」的根因**不能从现有代码直接确认**，必须靠日志与对照实验。

本轮交付：

- `ALTTABER_DIAG=1` 时才启用的诊断日志（生产运行默认不打印这些行）。
- `repo/tools/run-diag.bat`：纯 ASCII，设置 `QT_FORCE_STDERR_LOGGING=1`、`QT_WIN_DEBUG_CONSOLE=attach`，把 stdout/stderr 落到 `repo/build/Release/diag.log`。
- 复现协议文档：包含「退出 AltTaber 再复现一次」的对照实验。若退出后仍复现，则问题在 Windows 自身；若不复现，则按日志里的 `#FG` / `#SWITCH` 时间线定位是本程序哪条路径。

---

## 4. 实现步骤

> 所有步骤串行依赖：共享同一棵 `build/` 树，并行构建会互相踩。每步结束前必须编译通过。
> 编译命令统一为 `cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat"`，日志重定向到 `%TEMP%\alttaber_build.log`。
> 单测可执行文件在 `repo/build/tests/Release/`。

### s00 忽略构建产物并归档本轮文档

- 估时: 10m
- 依赖: -
- 提权: 否
- GUI: 否
- 判据: `git -C D:\a_my\project\Forks\AltTaber\repo status --porcelain` 的输出中不含 `build/` 与 `aqtinstall.log`
- 做法: 在 `repo/.gitignore` 追加 `build/` 与 `aqtinstall.log` 两行；不要删除 `build/` 目录；确认 `repo/docs/2026-10-17-multimonitor-and-dwm-thumbnail-plan.md` 在本步提交内。

### s01 纯几何函数与单测

- 估时: 40m
- 依赖: s00
- 提权: 否
- GUI: 否
- 判据: `repo/build/tests/Release/WindowGeometryTests.exe` 退出码为 0，且输出末行形如 `N/N passed`，`N >= 12`
- 做法: 新增 `header/utils/WindowGeometry.h`（不依赖 Qt/Win32，只用普通整数结构）与 `tests/window_geometry_test.cpp`；提供三个纯函数：`centerInWorkArea(w, h, work)`（居中并钳位到工作区；窗口大于工作区时贴左上）、`fitAspect(srcW, srcH, maxW, maxH)`（等比缩放，源尺寸非法时返回上限盒）、`thumbnailRect(listRect, thumbW, thumbH, gap, clientW, clientH)`（放在列表右侧、垂直居中、钳位在客户区内）；在 `CMakeLists.txt` 增加 `WindowGeometryTests` 可执行文件，输出格式照抄 `tests/window_diff_tracker_test.cpp`（逐条 `ok`/`FAIL`，末行 `N/N passed`，失败返回 1）。

### s02 屏幕映射工具与 `[monitor]` 配置读取

- 估时: 35m
- 依赖: s01
- 提权: 否
- GUI: 否
- 判据: `cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat"` 退出码为 0，且 `%TEMP%\alttaber_build.log` 中无 `warning C`；`grep -n "switch_mode" repo/header/utils/ConfigManager.h` 有输出
- 做法: 在 `header/utils/Util.h` / `src/utils/Util.cpp` 增加 `HMONITOR screenHandle(QScreen*)`（用 `QScreen::nativeInterface<QNativeInterface::QWindowsScreen>()->handle()`，取不到返回 nullptr）与 `HMONITOR monitorOfWindow(HWND)`（`MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)`）；在 `header/utils/ConfigManager.h` 增加 `enum MonitorSwitchMode { AllKeepPosition = 0, CurrentOnly = 1, AllMoveToCurrent = 2 }` 与 `getMonitorSwitchMode()`（读 `monitor/switch_mode`，越界回退 `AllKeepPosition` 并打 warning）。

### s03 Feat1 选项 1：按当前屏幕过滤

- 估时: 40m
- 依赖: s02
- 提权: 否
- GUI: 否
- 判据: `cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat"` 退出码为 0，日志无 `warning C`；`grep -c "CurrentOnly" repo/src/widget.cpp` 大于等于 3
- 做法: 在 `prepareWindowGroupList()` 枚举阶段、`orderedGroupWindows()`、`onNewWindows()` 三处，当 `cfg.getMonitorSwitchMode() == CurrentOnly` 时用 `Util::screenHandle(targetScreen())` 与 `Util::monitorOfWindow(hwnd)` 比较，跳过不在当前屏幕的窗口；`targetScreen()` 或 monitor 取不到时不过滤（保持旧行为）；`orderedGroupWindows()` 过滤后为空时 `enterGroupMode()` 正常返回 false。

### s04 Feat1 选项 2：切换时把目标窗口搬到当前屏幕

- 估时: 35m
- 依赖: s03
- 提权: 否
- GUI: 否
- 判据: `cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat"` 退出码为 0，日志无 `warning C`；`grep -c "moveWindowToMonitor" repo/src/widget.cpp` 大于等于 2
- 做法: 在 `src/utils/Util.cpp` 增加 `void moveWindowToMonitor(HWND hwnd, HMONITOR target)`：已在该 monitor 直接返回；用 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` 取外框、`GetMonitorInfo().rcWork` 取工作区，调用 s01 的 `centerInWorkArea()` 算新位置；最大化窗口先 `SW_RESTORE`、移动后再 `SW_MAXIMIZE`；用 `SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)` 移动；在 `Widget::keyReleaseEvent()` 的 AppMode 提交分支与 `Widget::commitGroupSelection()` 里，当 `cfg.getMonitorSwitchMode() == AllMoveToCurrent` 且 `targetScreen()` 非空时，先 `moveWindowToMonitor(hwnd, screenHandle(targetScreen()))` 再 `switchToWindow()`。

### s05 文档：多屏切换配置

- 估时: 15m
- 依赖: s04
- 提权: 否
- GUI: 否
- 判据: `grep -c "switch_mode" repo/CONFIG.md` 与 `grep -c "switch_mode" repo/CONFIG_zh.md` 都大于等于 2
- 做法: 在 `CONFIG.md` / `CONFIG_zh.md` 的完整示例与选项区加 `[monitor] switch_mode`，逐字写清 0/1/2 三种值、默认值 0、当前屏幕的定义、选项 2 的窗口摆放规则与 DPI 说明；说明该键在下次弹窗时生效，不需要重启。

### s06 DWM 缩略图封装类与配置

- 估时: 40m
- 依赖: s05
- 提权: 否
- GUI: 否
- 判据: `cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat"` 退出码为 0，日志无 `warning C`；`grep -n "DwmRegisterThumbnail" repo/src/utils/DwmThumbnail.cpp` 有输出
- 做法: 新增 `header/utils/DwmThumbnail.h` / `src/utils/DwmThumbnail.cpp`：构造时接收目标顶层窗口 HWND；`setSource(HWND)` 先注销旧源、再注册新源（源为空/等于目标/注册失败则只隐藏）；`setDestinationRect(const RECT&)` 用物理像素设置 `DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY`，并设 `fSourceClientAreaOnly = TRUE`（去标题栏与边框）；`hide()` 只关可见；析构注销。在 `header/utils/ConfigManager.h` 增加 `getGroupThumbnailEnabled()`、`getGroupThumbnailWidth()`（默认 320，钳 80..2000）、`getGroupThumbnailHeight()`（默认 180，钳 60..2000）。在 `CMakeLists.txt` 加入新源文件。

### s07 把缩略图集成进组列表布局

- 估时: 50m
- 依赖: s06
- 提权: 否
- GUI: 否
- 判据: `cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat"` 退出码为 0，日志无 `warning C`；`grep -n "thumbnail_" repo/src/widget.cpp` 有输出
- 做法: `Widget` 创建缩略图宿主：`QWidget` 带 `Qt::Tool | FramelessWindowHint | WindowStaysOnTopHint | WindowDoesNotAcceptFocus | WindowTransparentForInput`、parent = Widget、`WA_TranslucentBackground`；宿主 HWND 上用 `DWMWCP_ROUND` 加圆角（失败回退 `SetWindowRgn(CreateRoundRectRgn(...,16,16))`），再用 `DwmThumbnail` 注册缩略图；`layoutForCurrentMode()` 的 GroupMode 分支在 `preferredSize()` 之后按配置计算缩略图盒：源窗口用 `GetClientRect()` 取客户区尺寸（与 `fSourceClientAreaOnly=TRUE` 对齐），`WindowGeometry::fitAspect()` 得实际尺寸，`thumbnailRect()` 得逻辑矩形；弹窗内容宽度改为「列表宽 + 12 + 缩略图宽」、高度取两者较大值，列表与缩略图各自垂直居中；`setWidthCap()` 的屏幕兜底要减去缩略图宽度与间距；宿主位置由主弹窗全局坐标加逻辑矩形偏移、乘 `devicePixelRatioF()` 后用 `SetWindowPos` 设置；把宿主客户区物理矩形交给 `DwmThumbnail::setDestinationRect()`；高亮变化只重置一个 60ms 单次定时器，定时器到点才注册/更新缩略图，源 hwnd 未变则跳过；连接 `groupList_->widget()` 的 `currentItemChanged`、动态增删、`configEdited` 与 `hideEvent` 更新或隐藏缩略图与宿主；AppMode 下强制隐藏；源窗口 `IsIconic` 或 `Util::isWindowCloaked` 时隐藏；弹窗隐藏或切回 AppMode 时先停掉待处理的定时器。每步结束前用 s11 的 GUI 冒烟验证，不通过则回退到「缩略图关闭时布局逐像素不变」的最小实现。

### s08 文档：DWM 缩略图配置

- 估时: 10m
- 依赖: s07
- 提权: 否
- GUI: 否
- 判据: `grep -c "thumbnail_width" repo/CONFIG.md` 与 `grep -c "thumbnail_width" repo/CONFIG_zh.md` 都大于等于 1，且 `thumbnail_height` 同样
- 做法: 在 `CONFIG.md` / `CONFIG_zh.md` 的 `[group_list]` 节补 `thumbnail`、`thumbnail_width`、`thumbnail_height`，写清默认值、钳位范围、等比缩放、只在组列表显示、最小化窗口不显示、关闭时观感与旧版一致。

### s09 Fix1 诊断日志与探针启动脚本

- 估时: 40m
- 依赖: s08
- 提权: 否
- GUI: 否
- 判据: `cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat"` 退出码为 0，日志无 `warning C`；`grep -n "#FG\|#SWITCH\|#TB_\|#THUMB" repo/src -r` 有输出；`LC_ALL=C grep -n "[^ -~]" repo/tools/run-diag.bat` 无输出
- 做法: 新增 `header/utils/Diag.h`（`ALTTABER_DIAG` 环境变量只读一次）；在 `main.cpp` 的前台变化回调、`Widget::notifyForegroundChanged()`、`Widget::switchToWindow()` 的所有调用点、`TaskbarWheelHooker` 的进入/离开分支、`TaskListThumbnailWnd` 可见性变化处打印固定标记：`#FG`（hwnd/class/title/光标坐标/光标下窗口类名/缩略图窗口是否可见/本程序是否可见）、`#SWITCH`（调用点与目标）、`#TB_ENTER` / `#TB_LEAVE`（光标坐标与窗口类名）、`#THUMB`（可见性变化）；只有 `Diag::enabled()` 为真时打印。新增 `repo/tools/run-diag.bat`（纯 ASCII）：设置 `QT_FORCE_STDERR_LOGGING=1`、`QT_WIN_DEBUG_CONSOLE=attach`、`ALTTABER_DIAG=1`，启动 `repo\build\Release\AltTaber.exe` 并把 stdout/stderr 重定向到 `repo\build\Release\diag.log`。

### s10 Fix1 复现协议文档

- 估时: 15m
- 依赖: s09
- 提权: 否
- GUI: 否
- 判据: `test -s repo/docs/2026-10-17-taskbar-thumbnail-revert-repro.md`；`grep -c "ALTTABER_DIAG"` 与 `grep -c "#FG"` 都大于等于 1
- 做法: 写 `repo/docs/2026-10-17-taskbar-thumbnail-revert-repro.md`：准备步骤（退出已安装的 AltTaber、用 `tools\run-diag.bat` 启动探针版）、对照组（完全退出 AltTaber 后重复同样鼠标动作）、复现步骤（打开两个窗口、点任务栏缩略图切过去、鼠标朝上离开任务栏）、日志判读表（`#FG` / `#SWITCH` / `#TB_ENTER` / `#TB_LEAVE` 各自说明什么、出现哪种时间线对应哪条代码路径）、需要回收的产物路径。

### s11 GUI 冒烟：缩略图与列表回归

- 估时: 40m
- 依赖: s10
- 提权: 否
- GUI: 是
- 判据: 两张 windows-mcp 截图中，组列表右侧都有一块非背景色的预览区、四角为圆角且角落透出弹窗背景，且切换高亮行前后两张截图的预览区内容不同；组列表行数等于目标应用当时的窗口数
- 做法: 先读 `windows-mcp` skill；确认屏幕未锁；把 `repo/build/Release/config.ini` 的 `[group_list]` 增加 `thumbnail=true`、`thumbnail_width=320`、`thumbnail_height=180`；启动 `repo/build/Release/AltTaber.exe`；打开记事本两个窗口；在一个记事本里按 Alt+`` ` `` 呼出组列表，截图；按方向键切换高亮再截图；关闭 AltTaber，确认没有残留进程。若缩略图不出现，记录日志并回退 s07 的布局改动，保留关闭开关时的旧观感。

### s12 全量验收与交接

- 估时: 20m
- 依赖: s11
- 提权: 否
- GUI: 否
- 判据: `repo/build/tests/Release/WindowDiffTests.exe` 退出码为 0 且输出 `18/18 passed`；`repo/build/tests/Release/WindowGeometryTests.exe` 退出码为 0；`git -C D:\a_my\project\Forks\AltTaber\repo status --porcelain` 为空
- 做法: 跑一次全量 `rebuild.bat`（exit 0、无 `warning C`），跑两个单测；在 `HANDOFF.md` 末尾追加 2026-10-17 区段（本轮做了什么、哪些已验证、哪些未验证：多屏真机验收、选项 2 跨 DPI 观感、Fix1 根因待日志）；更新 docs 索引；确认工作树干净。

---

## 5. 验收与风险

### 夜间能验的

- 编译通过、无 warning；既有 `WindowDiffTests` 18/18；新增 `WindowGeometryTests` 全绿。
- 组列表缩略图在单屏上出现、随高亮变化（s11 截图）。
- 关闭 `thumbnail` 时组列表布局与改动前一致（s11 回归）。

### 夜间验不了、必须留给用户的

- **多屏真机行为**：2026-10-17 本机只接了一块屏，选项 1 的过滤与选项 2 的搬窗只能靠单测与代码审查，真机验收要等副屏接上。
- **跨 DPI 的窗口大小**：需要两块不同缩放率的屏幕，观察目标窗口跨屏后是否按 Windows 原生 `WM_DPICHANGED` 正常重排。
- **Fix1**：需要人按协议复现两次（含对照组），夜里只产出日志与文档。

### 风险

1. `layoutForCurrentMode()` 是唯一几何入口，s07 改它风险最高；关闭缩略图时必须回落到现有分支，保证逐像素不变。
2. DWM 缩略图已在等价窗口上实测可用，但真实窗口还有 `DwmEnableBlurBehindWindow` 的模糊与圆角；s11 是最终判据，失败时降级为不显示。
3. 选项 2 搬不动提权窗口（权限不足），`SetWindowPos` 失败时不报错；属于已知限制，写进文档。
4. 夜间 GUI 步骤要求屏幕未锁；锁屏时 s11 会失败，按任务单的失败处理，不重试。
5. 任务栏缩略图回退的根因未定，本轮**不写修复代码**，避免用猜测改坏既有切换路径。
6. 缩略图宿主是独立顶层窗口，可能带来 z-order、抢焦点、鼠标穿透或跨屏定位问题；已用 `WindowTransparentForInput`、`WindowDoesNotAcceptFocus`、`Qt::Tool` 与 Win32 `SetWindowPos` 规避，s11 是最终判据。若有问题，降级为不显示缩略图，不回退主弹窗布局。

---

## 6. 夜间任务单补充信息

### 元信息

- 目标: AltTaber 多屏切换三选项 + 组列表 DWM 缩略图 + 任务栏缩略图回退诊断日志
- 总预算: 8h
- 硬截止: 07:30
- 允许目录: D:\a_my\project\Forks\AltTaber, C:\Users\Yves\AppData\Local\Temp, D:\a_my\tool\Qt\6.8.1\msvc2022_64, D:\a_my\tool\vs2026
- 总花费上限: $40
- 提交仓库: D:\a_my\project\Forks\AltTaber\repo
- 提交分支: overnight/2026-10-17-multimonitor-thumbnail
- 框架科普源: D:\a_my\project\Forks\AltTaber\repo\docs\2026-10-17-multimonitor-and-dwm-thumbnail-plan.md
- 框架科普: 编译一律用 `cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat"`，输出在 `repo\build\Release`；AltTaber 运行时会锁住 exe，编译前先退出它
- 框架科普: GUI 程序日志要三件齐上才落盘：`QT_FORCE_STDERR_LOGGING=1`、`QT_WIN_DEBUG_CONSOLE=attach`、stdout/stderr 重定向；启动脚本用纯 ASCII
- 框架科普: Win32 的窗口/屏幕矩形与 DWM 缩略图坐标都是物理像素；Qt 逻辑坐标在 150% 缩放下要乘 1.5
- 框架科普: 本仓库 `build/` 未纳入版本控制；s00 之后不要提交任何构建产物

### 步骤表

| id | 标题 | 估时 | 依赖 | 提权 | GUI |
|---|---|---|---|---|---|
| s00 | 忽略构建产物并归档本轮文档 | 10m | - | 否 | 否 |
| s01 | 纯几何函数与单测 | 40m | s00 | 否 | 否 |
| s02 | 屏幕映射工具与 `[monitor]` 配置读取 | 35m | s01 | 否 | 否 |
| s03 | Feat1 选项 1：按当前屏幕过滤 | 40m | s02 | 否 | 否 |
| s04 | Feat1 选项 2：切换时把窗口搬到当前屏幕 | 35m | s03 | 否 | 否 |
| s05 | 文档：多屏切换配置 | 15m | s04 | 否 | 否 |
| s06 | DWM 缩略图封装类与配置 | 40m | s05 | 否 | 否 |
| s07 | 把缩略图集成进组列表布局 | 50m | s06 | 否 | 否 |
| s08 | 文档：DWM 缩略图配置 | 10m | s07 | 否 | 否 |
| s09 | Fix1 诊断日志与探针启动脚本 | 40m | s08 | 否 | 否 |
| s10 | Fix1 复现协议文档 | 15m | s09 | 否 | 否 |
| s11 | GUI 冒烟：缩略图与列表回归 | 40m | s10 | 否 | 是 |
| s12 | 全量验收与交接 | 20m | s11 | 否 | 否 |

估时合计 390m（6h30m），低于总预算 8h。

---

## 7. 起飞前的人工准备

1. 建提交分支（驱动只切不建）：
   ```bash
   git -C "D:/a_my/project/Forks/AltTaber/repo" branch overnight/2026-10-17-multimonitor-thumbnail
   ```
2. 确认屏幕在夜里**不锁**（s11 是 GUI 步骤）。
3. 副屏如果能接上，s11 之后可以顺手验选项 1 / 选项 2；否则留到白天。

---

## 8. 不在本轮范围

- 2026-09-30 known-issues 的未决项（假注释、AppMode 滚轮方向不对称、测试未接入 ctest、300ms 周期、提权窗口日志刷屏、CONFIG 措辞）。
- 动态扫描（`[dynamic]`）的验收与修复。
- 任务栏缩略图回退的实际修复（等日志）。
- `git push`（仍只在本机）。
