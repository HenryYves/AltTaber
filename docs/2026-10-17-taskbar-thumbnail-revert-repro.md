# 任务栏缩略图回退复现协议

- 日期：2026-10-17
- 适用构建：`overnight/2026-10-17-multimonitor-thumbnail` 分支，含 s09 诊断埋点的探针版
- 目的：固定「点任务栏缩略图切过去之后，鼠标朝上离开任务栏又切回原应用」这一现象，用 `ALTTABER_DIAG=1` 的诊断日志区分它是本程序的哪条代码路径，还是 Windows 自身行为
- 范围：**只收集证据，不改行为**。本协议不附带任何修复

---

## 0. 需要回收的产物

| 路径 | 内容 | 是否必须 |
| --- | --- | --- |
| `D:\a_my\project\Forks\AltTaber\repo\build\Release\diag.log` | 运行期诊断日志，含 `#FG` / `#SWITCH` / `#TB_ENTER` / `#TB_LEAVE` / `#THUMB` | 必须 |
| `%TEMP%\alttaber_build.log` | 探针版编译输出 | 仅编译失败或有告警时 |

`build/` 已被 `.gitignore` 排除，日志不会进提交；回收时直接取文件，或把 `grep` 结果贴进交接说明。

---

## 1. 准备

### 1.1 环境

- Windows 11，保持日常缩放（例如 150%）。日志里的 `cursor=` 是**物理像素**，不是 Qt 逻辑坐标；比对窗口位置时不要按逻辑坐标换算错误。
- 准备两个**同一个应用**的普通窗口（推荐两个记事本窗口，分别记为 A、B），左右并排，保证一眼能看出当前前台是哪一个。同一应用才会在任务栏上合成一个按钮并出现缩略图预览。

### 1.2 完全退出已安装的 AltTaber

已安装版本会占住 `AltTaber.exe`，且可能与探针版抢钩子，必须先退干净：

1. 在系统托盘右键 AltTaber 图标，选 `Quit >`。
2. 在 Git Bash 里确认没有残留进程：

   ```bash
   tasklist | grep -i "AltTaber.exe" || echo "no AltTaber"
   ```

   期望输出 `no AltTaber`。

注意：如果已安装版本是以管理员身份运行的，普通权限的 `taskkill` 结束不掉它，必须先走托盘 `Quit >`；不要用提权脚本硬杀。`tools\run-diag.bat` 开头虽然有一句 `taskkill /IM AltTaber.exe /F`，但它同样受权限限制。

### 1.3 用探针脚本启动

在 Git Bash 里执行：

```bash
cmd //c "D:\a_my\project\Forks\AltTaber\repo\tools\run-diag.bat"
```

脚本会做三件事：

1. 尝试结束已在运行的 `AltTaber.exe`；
2. 调用 `rebuild.bat` 重新编译，输出写到 `%TEMP%\alttaber_build.log`；失败则直接返回，不启动；
3. 设置 `QT_FORCE_STDERR_LOGGING=1`、`QT_WIN_DEBUG_CONSOLE=attach`、**`ALTTABER_DIAG=1`**，清空并重写 `repo\build\Release\diag.log`，然后启动 `repo\build\Release\AltTaber.exe`，把 stdout/stderr 追加到该日志。

### 1.4 确认探针生效

启动后先把鼠标移进任务栏再移出，然后查看日志：

```bash
tail -n 20 "D:\a_my\project\Forks\AltTaber\repo\build\Release\diag.log"
```

期望能看到 `==== AltTaber diag session ...` 表头，以及至少一行 `#TB_ENTER`、一行 `#TB_LEAVE`。若只有表头没有任何标记行，说明 `ALTTABER_DIAG` 没生效（它在进程内只读取一次，必须在启动前设置），重跑脚本，不要手工在已启动的进程上补环境变量。

---

## 2. 复现步骤（实验组 A：探针版正在运行）

每个动作之间停 1 到 2 秒，方便在日志里按时间切开。重复 3 轮，每轮记录是否出现回退。

1. 切到窗口 A，让 A 成为前台；确认 B 在它旁边可见。
2. 把鼠标移到任务栏上该应用的按钮，等缩略图预览弹出。此时日志应出现 `#THUMB`（`thumbVisible` 从 0 变 1），光标在任务栏上时还应先有 `#TB_ENTER`。
3. **点击窗口 B 的缩略图**（预览小窗，不是任务栏按钮本身），让 B 切到前台。记下点击时刻。
4. 不要点击任何其它窗口，把鼠标从缩略图位置**竖直朝上**移出任务栏，进入 B 的窗口区域。
5. 观察前台是否又变回 A。记下结果与大致时刻。
6. 恢复 A 为前台，重复第 2 到 5 步，共 3 轮。

可选变体（当日常复现路径里包含滚轮时追加）：在第 2 步之后，先在任务栏按钮上滚动一格呼出/切换组内窗口，再执行第 3 到 5 步。滚轮路径会走 `rotateTaskbarWindowInGroup()`，与单纯点击不是同一条代码路径，日志时间线也不同。

---

## 3. 对照组（实验组 B：AltTaber 完全退出）

1. 托盘右键 `Quit >`，并用 `tasklist | grep -i "AltTaber.exe" || echo "no AltTaber"` 确认没有进程。
2. **不要**再运行 `run-diag.bat`（它会重新拉起 AltTaber）。
3. 完全重复第 2 节的第 1 到 5 步（同一应用、两个窗口、点缩略图切到 B、鼠标朝上离开任务栏），同样做 3 轮。

判定：

| 对照组结果 | 结论 |
| --- | --- |
| 退出 AltTaber 后**仍然回退** | 现象与 AltTaber 无关，优先怀疑 Windows 任务栏自身或其它常驻软件；日志只用于排除本程序 |
| 退出 AltTaber 后**不再回退** | 现象由 AltTaber 参与，回到实验组 A 的日志，按第 5、6 节定位具体路径 |

---

## 4. 日志行格式

每条标记行都是单行，字段固定：

```text
#FG hwnd=0x00000000001A0B2C class="Notepad" title="a.txt - 记事本" cursor=812,1041 cursorClass="Shell_TrayWnd" thumbVisible=1 selfVisible=0
```

| 字段 | 含义 | 判读注意 |
| --- | --- | --- |
| `hwnd` | 本次事件的「主角」窗口 | 不同标记里主角不同：`#FG` 是新的前台窗口，`#SWITCH` 是即将切过去的目标，`#TB_ENTER` 是任务栏，`#TB_LEAVE` 是光标下新的顶层窗口 |
| `class` / `title` | 主角窗口的类名与标题 | 标题里的引号与换行已被压成单行；中文标题在日志里可能按 UTF-8 原样出现 |
| `cursor` | 记录瞬间的鼠标位置，**物理像素** | 用来确认「离开任务栏」时鼠标到底在哪 |
| `cursorClass` | 光标正下方顶层窗口的类名 | 判断光标是在任务栏、缩略图预览、还是应用窗口上 |
| `thumbVisible` | 光标所在显示器上的 `TaskListThumbnailWnd` 是否可见 | 1 表示缩略图预览还在，0 表示已消失；隐藏时同一行可能显示 `hwnd=0x0` |
| `selfVisible` | AltTaber 自己的切换器窗口是否可见 | 1 表示 AltTaber 弹窗在场，可能参与抢焦点 |

两个容易误读的点：

- 由 `EVENT_SYSTEM_FOREGROUND` 触发的前台变化会**连续出现两条同 hwnd 的 `#FG`**：`main.cpp:69` 一条，`Widget::notifyForegroundChanged()` 内部（`widget.cpp:331`）一条。这不是切了两次。
- 日志里同时混有普通 `qDebug` 行（例如 `(Taskbar)Wheel on:`、`(Taskbar)#Press LButton`、`#Enter Taskbar`）。判读代码路径时以标记行为骨架，用这些普通行做上下文。

---

## 5. 标记判读表

| 标记 | 写入位置 | 说明什么 |
| --- | --- | --- |
| `#FG` | `src/main.cpp:69`、`src/widget.cpp:331` | 前台窗口发生变化。主角是**新的前台窗口**。`selfVisible=1` 且 `class` 是本程序窗口时，表示 AltTaber 自己抢到了前台；`cursorClass="Shell_TrayWnd"` 表示前台变化发生时鼠标还停在任务栏上 |
| `#SWITCH` | `TaskbarWheelHooker.cpp:21`、`widget.cpp:307 / 1051 / 1253 / 1257 / 1277` | AltTaber **主动调用** `Util::switchToWindow()` 的前一刻。主角是它要切过去的窗口。每一处的判别见第 6 节 |
| `#TB_ENTER` | `TaskbarWheelHooker.cpp:66` | 50ms 轮询发现光标下的顶层窗口从「非任务栏」变成 `Shell_TrayWnd` / `Shell_SecondaryTrayWnd`，随即安装滚轮钩子。主角是任务栏窗口 |
| `#TB_LEAVE` | `TaskbarWheelHooker.cpp:71` | 轮询发现光标下的顶层窗口不再是任务栏，卸载滚轮钩子，并排队发出 `leaveTaskbar` → `Widget::clearGroupWindowOrder()`（`widget.cpp:1328`）。**这条路径本身不切窗**。主角是光标下新的顶层窗口 |
| `#THUMB` | `TaskbarWheelHooker.cpp:84` | `TaskListThumbnailWnd` 可见性发生变化。只在 `ALTTABER_DIAG=1` 时轮询，主角是该缩略图窗口；隐藏时可能是 `hwnd=0x0` |

关于 `#TB_LEAVE` 的边界：`Util::isTaskbarWindow()` 只认 `Shell_TrayWnd` / `Shell_SecondaryTrayWnd` 两个类名。光标从任务栏移到**缩略图预览窗口**（类名 `TaskListThumbnailWnd`）上时，也会被判成「离开任务栏」，哪怕视觉上还在任务栏区域。因此看到 `#TB_LEAVE` 时先看 `cursorClass`：

- `cursorClass="TaskListThumbnailWnd"`：只是进入了缩略图预览；
- `cursorClass="Notepad"` 等应用类名：确实离开了任务栏。

---

## 6. 时间线 → 代码路径

按日志中出现顺序对照。先看回退发生的那一刻（前台变回 A、出现 A 的 `#FG`）之前最近一条 `#SWITCH` 是什么。

### 6.1 点击缩略图切到 B 的瞬间

```text
#THUMB ... thumbVisible=1        （缩略图预览出现）
#FG   hwnd=<B> ...               （成对出现两条）
#FG   hwnd=<B> ...
```

没有 `#SWITCH`。这是正常的：AltTaber 的鼠标钩子只处理 `WM_MOUSEWHEEL`，物理点击会直接传给系统，切窗是 Windows 自己完成的。这一段的日志只作为基线。

### 6.2 回退时的时间线

| 观察到的序列 | 对应代码路径 | 说明 |
| --- | --- | --- |
| `#TB_LEAVE` 之后约 100 到 300ms 内出现 `#SWITCH hwnd=<Shell_TrayWnd>`，随后 `#FG hwnd=<A>` | `widget.cpp:1273-1279` | 滚轮路径按过左键后，`QTimer::singleShot(100ms)` 检查缩略图仍可见，就 `switchToWindow(taskbar, true)` 把焦点交给任务栏；`leaveTaskbar` 不会取消这个已经排队的定时器。焦点落到任务栏后，Windows 可能把前台还给它上一个窗口 A，看起来就是「切回原应用」。**这是重点怀疑路径** |
| `#TB_LEAVE` 之后约 20ms 内出现 `#SWITCH hwnd=<B>` | `widget.cpp:1252-1255` | 滚轮路径发现缩略图可见，先模拟左键按下，再延迟 20ms 切到 `hwnd`。若此前是滚轮触发的切换，鼠标上移期间定时器照样会响 |
| `#SWITCH hwnd=<B>`，前面紧挨 `(Taskbar)Wheel on:` | `widget.cpp:1257` | 滚轮路径发现缩略图不可见时立即切窗 |
| `#SWITCH hwnd=<A>`，前面有 Alt 松手相关上下文 | `widget.cpp:307`（AppMode）或 `widget.cpp:1051`（GroupMode） | 只有按下过 Alt 才会走；单纯鼠标点击复现时不应出现 |
| `#SWITCH hwnd=<Shell_TrayWnd>`，前面有 `detect CEF, try active taskbar` | `TaskbarWheelHooker.cpp:21` | 仅当滚轮落在 NVIDIA Overlay（`CEF-OSC-WIDGET`）上时的兜底；主角是任务栏 |
| `#TB_LEAVE` 之后回退真的发生，但整段**没有任何 `#SWITCH`** | 不是上述任何切窗调用 | `leaveTaskbar` 只清空 `groupWindowOrder`，不切窗。此时回退来自 AltTaber 之外（Windows 自身或其它软件），对照组的结论优先 |

补充：任务栏滚轮的**单窗口**分支（`widget.cpp:1234-1241`）用 `mouse_event` 模拟一次左键点击，**不写 `#SWITCH`**。它只在任务栏滚轮时触发；如果日志里缺少 `#SWITCH` 但此前出现过 `(Taskbar)Wheel on:` 且 `windows == 1`，不能仅凭「没有 `#SWITCH`」排除这条路径。

### 6.3 一句话判定

- 对照组也回退：问题不在 AltTaber，停止按日志归因。
- 对照组不回退，且回退前最近一条 `#SWITCH` 的主角是 `Shell_TrayWnd`：按 6.2 第一行查 `widget.cpp:1273-1279` 的 100ms 定时器。
- 对照组不回退，且回退前最近一条 `#SWITCH` 的主角是 A：按第 6.2 表的 `#SWITCH` 分类定位到 307 / 1051 / 1253 / 1257。
- 对照组不回退，但回退时没有任何 `#SWITCH`：需要继续排查其它常驻软件或 Windows 行为，日志已足以排除本程序的显式切窗调用。

---

## 7. 收尾

1. 托盘 `Quit >` 退出探针版，确认 `tasklist | grep -i "AltTaber.exe"` 无输出。
2. 回收 `D:\a_my\project\Forks\AltTaber\repo\build\Release\diag.log`。若实验分多轮进行，不要中途重跑 `run-diag.bat`，它会在启动时覆盖该文件。
3. 用下面的命令先筛出骨架，再按第 6 节对照：

   ```bash
   grep -nE "#FG|#SWITCH|#TB_ENTER|#TB_LEAVE|#THUMB" \
     "D:\a_my\project\Forks\AltTaber\repo\build\Release\diag.log"
   ```

4. 把回退发生的时刻、落回的窗口标题、对照组结论，与上述 `grep` 片段一起写进交接说明。
