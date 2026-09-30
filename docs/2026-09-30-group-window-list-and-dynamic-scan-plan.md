# AltTaber Alt+` 进程内窗口列表 + 动态扫描 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 Alt+` 从"无界面盲切"改成"玻璃背景的垂直窗口列表"，并让两个列表在显示期间以 300ms 轮询，把新出现的后台窗口动态入列、可选自动选中。

**Architecture:** 共用一个 `Widget` HWND（`KeyboardHooker` 只持有唯一静态 receiver，新增窗口会带来焦点与事件管线的重复），内部加 `Mode{AppMode, GroupMode}` 切换。新增三个单元：`WindowDiffTracker`（纯 C++ 的集合 diff，唯一可单测的部分）、`WindowWatcher`（Qt 定时器胶水）、`GroupWindowList`（竖排列表的展示与选中）。

**Tech Stack:** C++20 / Qt 6.8.1 (QtWidgets) / MSVC (VS 2026) / CMake 3.29+，生成器 `Visual Studio 18 2026` -A x64。

**设计依据：** `2026-09-30-group-window-list-and-dynamic-scan-design.md`（本计划与它的偏差见文末 §偏差记录）

## Global Constraints

- 平台：Windows 11 + MSVC（VS 2026），**不是** MinGW。
- C++20，Qt 6.8.1，`CMAKE_PREFIX_PATH` 已指向 `D:/a_my/tool/Qt/6.8.1/msvc2022_64/lib/cmake`。
- **不要修改 `CMakeLists.txt:8` 的 `CMAKE_PREFIX_PATH`**（既有的本机路径硬编码，本次不动）。
- 构建目录 `repo/build`；工具链入口 `D:\a_my\tool\vs2026\VC\Auxiliary\Build\vcvars64.bat`。
- 源码为 UTF-8（部分带 BOM），含中文注释。新文件必须也是 UTF-8，且新 target 必须加 `/utf-8`，否则 MSVC 会按本地代码页解释中文导致乱码或编译错。
- **UI 里禁止任何 emoji**（用户硬性要求）。所有界面元素只用文字或他提供的图。
- 配置键名必须**逐字**如下，写在 `config.ini`：
  - `[dynamic] watch_new_windows`（bool，默认 `false`）
  - `[dynamic] auto_select_new`（bool，默认 `false`）
- 提交信息用英文 conventional commits 风格（与既有 4 个提交一致）。
- **本仓没有任何测试框架**。GUI 行为**无法自动验证**——凡是标了「交用户跑」的步骤，实现者**不得**自行声称已验证。

### 构建与运行命令（Git Bash 里照抄）

**全量构建**（会跑几分钟；一次编出 `AltTaber.exe` **和** `WindowDiffTests.exe` 两个目标，已实测通过）：
```bash
"D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/rebuild.bat"
```
产物：`repo/build/Release/AltTaber.exe`、`repo/build/tests/Release/WindowDiffTests.exe`

> **不要**用 `cmd //c "call \"…\" >nul && …"` 这种内联形式——实测它会把字面量 `\"` 传给 cmd，命令直接失败。需要把多条命令串起来时，写一个临时 `.bat` 到 `%TEMP%` 再执行，用完删掉。

**只构建测试 target**（全量构建的子集，想省时间时用）：把下面内容写成 `%TEMP%\build_tests.bat` 再执行——
```bat
@echo off
call "D:\a_my\tool\vs2026\VC\Auxiliary\Build\vcvars64.bat" > nul
cd /d "D:\a_my\project\workspaces_AND_tool-agnostic-projects\Daily_Claude\project\2026-06-16_alttaber_research\repo\build"
cmake --build . --config Release --target WindowDiffTests
```

**运行测试**：
```bash
"D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo/build/tests/Release/WindowDiffTests.exe"
```

### 抓 AltTaber 自己的日志（实测可用的配方）

AltTaber 是 `WIN32_EXECUTABLE true`（GUI 子系统），**没有控制台**，Qt 默认把日志送去 `OutputDebugString`，直接跑看不到任何东西。2026-09-30 实测踩了三种写法才找到能用的：

- ❌ 直接 `./AltTaber.exe 2>log` —— 0 字节
- ❌ 经 `cmd.exe`（临时 `.bat`）转发、只重定向 —— 0 字节
- ❌ 只设 `QT_WIN_DEBUG_CONSOLE=attach` —— 0 字节
- ✅ **`set QT_FORCE_STDERR_LOGGING=1` + `set QT_WIN_DEBUG_CONSOLE=attach` + 重定向** —— 拿到完整日志

写一个纯 ASCII 的临时 `.bat`（中文内容会让 .bat 的 GBK 编码问题插进来，所以 .bat 里只放路径）：

```bat
@echo off
set QT_FORCE_STDERR_LOGGING=1
set QT_WIN_DEBUG_CONSOLE=attach
"<repo>\build\Release\AltTaber.exe" > "<临时目录>\at_out.txt" 2> "<临时目录>\at_log.txt"
```
从 Git Bash 直接按路径执行这个 `.bat`（实测可行），用完删掉。日志是 UTF-8；在 Git Bash 里看中文要 `iconv -f UTF-8 -t GBK`，但数 `[watch]` 这类 ASCII 行不需要。

**前置条件**：AltTaber 正在运行时会持有单实例锁（`QSharedMemory "AltTaber-MrBeanCpp"`），第二个实例弹框即退。**先彻底退出正在运行的实例**。

**两个已踩过的坑：**
- `deploy.bat` 末尾有一句 `pause`，**非交互执行会永久挂住**。要部署就手动跑那一行 `windeployqt.exe`，或在交互式终端里跑。
- 三个 `.bat` 内部路径写的是大写 `Project`，实际目录是小写 `project`。Windows 不区分大小写所以能跑，但**不要在别处照抄这个大小写**。

---

### Task 1: 测试脚手架 + `WindowDiffTracker`

先把唯一的纯逻辑单元做出来，并且**证明这个测试真的会红**——这是后面所有 GUI 任务唯一的自动化防线。

**Files:**
- Create: `repo/header/utils/WindowDiffTracker.h`
- Create: `repo/tests/window_diff_tracker_test.cpp`
- Modify: `repo/CMakeLists.txt`（在文件末尾追加）

**Interfaces:**
- Consumes: 无（纯 `<Windows.h>` + STL）
- Produces:
  - `Util::WindowDiff { std::vector<HWND> added; std::vector<HWND> removed; bool empty() const; }`
  - `Util::WindowDiffTracker::update(const std::vector<HWND>& current) -> WindowDiff`
  - `Util::WindowDiffTracker::seed(const std::vector<HWND>& current) -> void`
  - `Util::WindowDiffTracker::reset() -> void`
  - `Util::WindowDiffTracker::knownCount() const -> size_t`
  - 契约：`added` / `removed` **都按句柄值升序**输出，与输入顺序无关；`update()` 在空 tracker 上把所有窗口算作 `added`；`seed()` 只记录不产生差异。

- [ ] **Step 1: 写头文件（先只写声明，故意不写实现）**

`repo/header/utils/WindowDiffTracker.h`：
```cpp
#ifndef WIN_SWITCHER_WINDOWDIFFTRACKER_H
#define WIN_SWITCHER_WINDOWDIFFTRACKER_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <set>
#include <vector>

namespace Util {

/// 一轮窗口集合的差异。added/removed 均按句柄值升序。
struct WindowDiff {
    std::vector<HWND> added;
    std::vector<HWND> removed;
    bool empty() const { return added.empty() && removed.empty(); }
};

/// 记住"上一轮见过哪些窗口"，用来在轮询时找出新出现 / 已消失的窗口。
/// 纯数据 + 纯 STL，不依赖 Qt、不调用任何 Win32 API —— 这是本改动里
/// 唯一能脱离 GUI 做单元测试的部分。
class WindowDiffTracker {
public:
    /// 与上一次的集合做差异，然后把 known_ 更新为 current
    WindowDiff update(const std::vector<HWND>& current);

    /// 只记录，不产生差异。用于"开始轮询时先把现有窗口吃进去"，
    /// 否则第一轮会把所有已有窗口都报成新增。
    void seed(const std::vector<HWND>& current);

    void reset();
    size_t knownCount() const { return known_.size(); }

private:
    std::set<HWND> known_;
};

} // namespace Util

#endif //WIN_SWITCHER_WINDOWDIFFTRACKER_H
```

- [ ] **Step 2: 写测试文件**

`repo/tests/window_diff_tracker_test.cpp`：
```cpp
// 纯 C++ 测试，不依赖 Qt / 不依赖任何测试框架。
// 运行后在 stdout 打印每条断言的结果，全部通过时返回 0，否则返回 1。
#include "utils/WindowDiffTracker.h"
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

static int g_total = 0;
static int g_failed = 0;

static void check(bool cond, const std::string& what) {
    ++g_total;
    if (cond) {
        std::printf("ok   %s\n", what.c_str());
    } else {
        ++g_failed;
        std::printf("FAIL %s\n", what.c_str());
    }
}

static HWND h(intptr_t v) { return reinterpret_cast<HWND>(v); }

static std::vector<HWND> vec(std::initializer_list<intptr_t> vs) {
    std::vector<HWND> out;
    for (auto v: vs) out.push_back(h(v));
    return out;
}

static int main_impl() {
    using Util::WindowDiffTracker;

    // 1. 空 tracker 的第一次 update：全部算新增，没有消失
    {
        WindowDiffTracker t;
        auto d = t.update(vec({1, 2, 3}));
        check(d.added == vec({1, 2, 3}), "empty tracker: all current windows are added");
        check(d.removed.empty(), "empty tracker: nothing removed");
        check(t.knownCount() == 3, "known count becomes 3");
    }

    // 2. 集合完全不变 → 无差异
    {
        WindowDiffTracker t;
        t.update(vec({1, 2, 3}));
        auto d = t.update(vec({1, 2, 3}));
        check(d.empty(), "unchanged set produces an empty diff");
    }

    // 3. 只增不删
    {
        WindowDiffTracker t;
        t.update(vec({1, 2}));
        auto d = t.update(vec({1, 2, 3}));
        check(d.added == vec({3}), "one new window is reported as added");
        check(d.removed.empty(), "no removals when a window appears");
    }

    // 4. 只删不增
    {
        WindowDiffTracker t;
        t.update(vec({1, 2, 3}));
        auto d = t.update(vec({1, 3}));
        check(d.added.empty(), "no additions when a window disappears");
        check(d.removed == vec({2}), "one closed window is reported as removed");
    }

    // 5. 同时增删
    {
        WindowDiffTracker t;
        t.update(vec({1, 2}));
        auto d = t.update(vec({2, 9}));
        check(d.added == vec({9}), "simultaneous: correct addition");
        check(d.removed == vec({1}), "simultaneous: correct removal");
    }

    // 6. 输出与输入顺序无关（两个方向都试）
    {
        WindowDiffTracker t;
        t.update(vec({1, 2, 3}));
        auto a = t.update(vec({7, 5}));  // 降序传入
        check(a.added == vec({5, 7}), "added is sorted ascending regardless of input order");
        check(a.removed == vec({1, 2, 3}), "removed is sorted ascending");
    }

    // 7. seed 之后不报新增（这是"列表刚弹出时不该入列任何东西"的防线）
    {
        WindowDiffTracker t;
        t.seed(vec({1, 2, 3}));
        auto d = t.update(vec({1, 2, 3}));
        check(d.empty(), "seed() suppresses the first-round false additions");
        check(t.knownCount() == 3, "seed() records the windows");
    }

    // 8. reset 之后回到"全部算新增"
    {
        WindowDiffTracker t;
        t.update(vec({1, 2, 3}));
        t.reset();
        check(t.knownCount() == 0, "reset clears known set");
        auto d = t.update(vec({1}));
        check(d.added == vec({1}), "after reset the window counts as added again");
    }

    // 9. 输入里的重复句柄只算一个
    {
        WindowDiffTracker t;
        auto d = t.update(vec({4, 4, 5}));
        check(d.added == vec({4, 5}), "duplicate handles collapse to one");
        check(t.knownCount() == 2, "duplicate handles counted once");
    }

    return g_failed == 0 ? 0 : 1;
}

int main() {
    const int rc = main_impl();
    std::printf("\n%d/%d passed\n", g_total - g_failed, g_total);
    return rc;
}
```

- [ ] **Step 3: 加 CMake target**

在 `repo/CMakeLists.txt` **末尾**追加：
```cmake
# ---- 单元测试：纯 C++，不依赖 Qt ----
add_executable(WindowDiffTests tests/window_diff_tracker_test.cpp)
set_target_properties(WindowDiffTests PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/$<CONFIG>")
if (MSVC)
    target_compile_options(WindowDiffTests PRIVATE "/utf-8")
endif ()
```
说明：`include_directories(header)` 在 `CMakeLists.txt:31`，对本目录下所有 target 生效，所以 `#include "utils/WindowDiffTracker.h"` 直接可用，不需要额外 include 目录。必须显式关掉 AUTOMOC/AUTOUIC/AUTORCC，否则全局的 `set(CMAKE_AUTOMOC ON)` 会在这个没有 Qt 的 target 上做无谓的扫描。

- [ ] **Step 4: 构建，确认它**红**（此时 `impl` 不存在，链接会失败）**

Run:
```bash
cmd //c "call \"D:\a_my\tool\vs2026\VC\Auxiliary\Build\vcvars64.bat\" >nul && cd /d \"D:\a_my\project\workspaces_AND_tool-agnostic-projects\Daily_Claude\project\2026-06-16_alttaber_research\repo\build\" && cmake .. -G \"Visual Studio 18 2026\" -A x64 && cmake --build . --config Release --target WindowDiffTests"
```
Expected: FAIL —— 链接错误，`update` / `seed` / `reset` 未解析的外部符号。

- [ ] **Step 5: 写实现（补进同一个头文件，`namespace Util` 内、类定义之后）**

```cpp
inline WindowDiff WindowDiffTracker::update(const std::vector<HWND>& current) {
    // std::set 迭代天然按句柄值升序，保证输出的确定性
    std::set<HWND> next(current.begin(), current.end());
    WindowDiff diff;
    for (auto hwnd: next)
        if (known_.find(hwnd) == known_.end())
            diff.added.push_back(hwnd);
    for (auto hwnd: known_)
        if (next.find(hwnd) == next.end())
            diff.removed.push_back(hwnd);
    known_ = std::move(next);
    return diff;
}

inline void WindowDiffTracker::seed(const std::vector<HWND>& current) {
    known_ = std::set<HWND>(current.begin(), current.end());
}

inline void WindowDiffTracker::reset() {
    known_.clear();
}
```

- [ ] **Step 6: 构建并运行，确认全绿**

Run: 同 Step 4 的构建命令，然后
```bash
"D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo/build/tests/Release/WindowDiffTests.exe"
```
Expected: 9 组、**共 18 条**断言全部 `ok`，最后一行 `18/18 passed`，退出码 0。
（本 Task 已执行完毕，实测结果即 `18/18 passed`。若将来数字变了，说明断言被改动过。）

- [ ] **Step 7: 变异测试 —— 证明这个测试真的会红**

**这是本 Task 最重要的一步**，不做的话上面的绿等于没绿。逐条改坏实现、确认测试变红、再改回来：

1. 把 `update()` 里 `diff.added.push_back(hwnd)` 改成 `diff.removed.push_back(hwnd)`
   → 预期：多条变红（**实测 10 条**）
2. 把 `seed()` 的函数体改成 `known_.clear();`（等于没播种）
   → 预期：`seed() suppresses the first-round false additions` 必须变红（**实测 1 条**）
3. 把 `update()` 里最后一行 `known_ = std::move(next);` 删掉
   → 预期：多条变红，因为此后每一轮都会把所有窗口重报一遍（**实测 9 条**）
4. 把 `update()` 里 `for (auto hwnd: next)` 改成 `for (auto hwnd: current)`
   → 预期：重复句柄相关的断言变红（**实测 2 条**）

每改一条，重新构建 + 运行，**记录实际变红的断言**。

**判据是"每一条变异都必须变红"，不是"红了几条对上"。** 上面括号里的实测数字只是回归基线，供将来对照；数量对不上不算失败，"改了却全绿"才算——那种情况必须当场查清，不许放过。

**已知的覆盖缺口（复审 2026-09-30 指出，尚未验证）**：上面这 4 条变异打不红两条断言——
`seed() records the windows`（要 `seed` 和 `update` **同时**停止记录才会红，单点变异打不中）
和 `reset clears known set`（没有任何一条变异针对 `reset()`）。它们的"能红"答案是存在的
（`reset(){}` 空实现；把 `removed` 那个循环的 `next` 换成 `current`），只是当前变异集没覆盖。
**下次修订本基线时把这两条补进去**，补完要实跑一遍、把实测数字写回这里——**不要**只把指令写进来
就当成已验证。

- [ ] **Step 8: 提交**

```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git add header/utils/WindowDiffTracker.h tests/window_diff_tracker_test.cpp CMakeLists.txt
git commit -m "test: add pure-C++ window diff tracker with mutation-checked tests"
```

---

### Task 2: `WindowWatcher`（Qt 定时器胶水）

**Files:**
- Create: `repo/header/utils/WindowWatcher.h`
- Create: `repo/src/utils/WindowWatcher.cpp`
- Modify: `repo/CMakeLists.txt`（加进 `Win_Switcher` 的源文件列表）
- Modify: `repo/src/main.cpp`（**临时的**调试连接，Task 9 会删掉）

**Interfaces:**
- Consumes: `Util::WindowDiffTracker`（Task 1）、`Util::listValidWindows()`
- Produces:
  - `class WindowWatcher : QObject`，`static constexpr int IntervalMs = 300`
  - `void start()` / `void stop()` / `bool isRunning() const`
  - `signals: void newWindowsFound(const QList<HWND>& added); void windowsGone(const QList<HWND>& removed);`
  - 契约：`start()` 会用当前窗口集合播种，**第一轮不会把已有窗口报成新增**。

- [ ] **Step 1: 写头文件**

`repo/header/utils/WindowWatcher.h`：
```cpp
#ifndef WIN_SWITCHER_WINDOWWATCHER_H
#define WIN_SWITCHER_WINDOWWATCHER_H

#include <QObject>
#include <QTimer>
#include <QList>
#include <Windows.h>
#include "utils/WindowDiffTracker.h"

/// 列表显示期间轮询窗口集合，把"新出现/已消失"的窗口报出去。
/// 只在列表可见时运行，不常驻后台。
class WindowWatcher : public QObject {
    Q_OBJECT

public:
    /// 300ms。依据：Util::listValidWindows() 自注释约 2ms（Util.cpp:323），
    /// 占空比 < 1%；且远低于"注意到新窗口出现"的感知阈值。
    static constexpr int IntervalMs = 300;

    explicit WindowWatcher(QObject* parent = nullptr);

    /// 开始轮询。先用当前窗口集合播种，因此第一轮不会把已有窗口报成"新增"
    void start();
    /// 停止轮询并清空已知集合
    void stop();
    bool isRunning() const { return timer_.isActive(); }

signals:
    void newWindowsFound(const QList<HWND>& added);
    void windowsGone(const QList<HWND>& removed);

private:
    QTimer timer_;
    Util::WindowDiffTracker tracker_;
};

#endif //WIN_SWITCHER_WINDOWWATCHER_H
```

- [ ] **Step 2: 写实现**

`repo/src/utils/WindowWatcher.cpp`：
```cpp
#include "utils/WindowWatcher.h"
#include "utils/Util.h"

namespace {
    /// QList<HWND> -> std::vector<HWND>
    /// 不用 QList::toVector()，避免 Qt 版本间 API 可用性差异
    std::vector<HWND> toVector(const QList<HWND>& list) {
        std::vector<HWND> out;
        out.reserve(size_t(list.size()));
        for (auto hwnd: list) out.push_back(hwnd);
        return out;
    }
}

WindowWatcher::WindowWatcher(QObject* parent) : QObject(parent) {
    timer_.setInterval(IntervalMs);
    connect(&timer_, &QTimer::timeout, this, [this] {
        auto diff = tracker_.update(toVector(Util::listValidWindows()));
        if (!diff.added.empty())
            emit newWindowsFound(QList<HWND>(diff.added.begin(), diff.added.end()));
        if (!diff.removed.empty())
            emit windowsGone(QList<HWND>(diff.removed.begin(), diff.removed.end()));
    });
}

void WindowWatcher::start() {
    tracker_.seed(toVector(Util::listValidWindows())); // 播种，见头文件契约
    if (!timer_.isActive()) timer_.start();
}

void WindowWatcher::stop() {
    timer_.stop();
    tracker_.reset();
}
```

- [ ] **Step 3: 加进 CMake**

在 `repo/CMakeLists.txt` 的 `add_executable(Win_Switcher ...)` 源文件列表里，紧跟 `header/utils/ConfigManagerBase.h` 那一行之后插入两行：
```cmake
        src/utils/WindowWatcher.cpp
        header/utils/WindowWatcher.h
```

- [ ] **Step 4: 加临时调试连接（脚手架，Task 9 会替换掉）**

在 `repo/src/main.cpp` 里 `new Widget` 附近，**临时**加：
```cpp
    //! 临时脚手架：验证 WindowWatcher 的信号，Task 9 会用真实处理替换掉这段
    auto* dbgWatcher = new WindowWatcher(&a);
    QObject::connect(dbgWatcher, &WindowWatcher::newWindowsFound, dbgWatcher, [](const QList<HWND>& added) {
        qInfo() << "[watch] new windows:" << added;
    });
    QObject::connect(dbgWatcher, &WindowWatcher::windowsGone, dbgWatcher, [](const QList<HWND>& gone) {
        qInfo() << "[watch] gone windows:" << gone;
    });
    dbgWatcher->start();
```
并在文件头部加 `#include "utils/WindowWatcher.h"`。

- [ ] **Step 5: 构建**

Run: `rebuild.bat`（见 Global Constraints）
Expected: 构建成功，无新增 warning。

- [ ] **Step 6: 验证播种逻辑（抓"第一轮全报新增"这个失效模式）**

**先记住一个硬事实，否则这一步会静默假阴性**：`AltTaber` 是 `WIN32_EXECUTABLE true`（`CMakeLists.txt:97`）= `/SUBSYSTEM:WINDOWS`，**没有控制台**。Qt 6.8 文档（`debug.html`）原文：*"On Windows, output is sent to the console for console applications or to the debugger for GUI applications."* 所以 `qInfo()` **不写终端**，只写 `OutputDebugString`。**直接跑起来"看控制台输出"会是一片空白**，然后被误判成"播种失效"。要拿到日志，用 Qt 6.8 同页文档给的 `QT_WIN_DEBUG_CONSOLE`（把 stdout/stderr 重定向到控制台、或附加到父进程控制台）。它与 `2>` 文件重定向能否共存**需实测**，不要假设。

**前置条件**：AltTaber 正在跑时会持有单实例锁（`QSharedMemory "AltTaber-MrBeanCpp"`；`SingleApp` 检查在 `main.cpp` 里发生在脚手架**之前**），第二个实例会弹框退出——那样三条检查会"全部通过"得毫无意义。**必须先彻底退出正在运行的 AltTaber。**

**这三条不需要任何桌面手工操作，由验证者自己跑，不要交给用户**：

1. **静置**：启动后什么都不做，观察 10 秒
2. **阳性对照**：`cmd /c start "" notepad` —— 记下 PID
3. **关闭对照**：`taskkill /PID <刚记下的 PID>` —— **只杀自己起的那个**；绝不用 `taskkill /IM notepad.exe`，那会把用户自己开着的记事本一起杀掉

**判据是"流"，不是"计数"**：静置期间出现**孤立的一对** `new windows` + 对应的 `gone windows`（同一个 HWND），或者输出只集中在启动后头一两秒，都**不算失败**——输入法候选窗、toast、`ForegroundStaging`（本仓 `main.cpp:83` 自己就特判过它）这类瞬态窗口本来就会产生这样一对。**真正的失败信号是连续不断的流**，或者启动几秒之后、用户毫无动作却仍在持续冒出来的行。

任何一条没拿到，先查清再往下走。**未经实跑，不得记为通过。**

- [ ] **Step 7: 提交**

```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git add header/utils/WindowWatcher.h src/utils/WindowWatcher.cpp CMakeLists.txt src/main.cpp
git commit -m "feat(watch): add WindowWatcher polling new/closed windows"
```

---

### Task 3: 给 `getBlacklistFileNames()` 加缓存

轮询会把它从"每窗口一次"变成"每秒 N×3 次"，而它每次都重读 QSettings 并重新 split+toLower。这是本次改动**放大**出来的既有低效。

**Files:**
- Modify: `repo/header/utils/ConfigManager.h:53-63`（以及 class 的 private 区）

**Interfaces:**
- Consumes: `ConfigManagerBase::configEdited` 信号（`ConfigManagerBase.h:67`）
- Produces: `QStringList getBlacklistFileNames()` 签名不变，行为不变，只是加了缓存与失效

- [ ] **Step 1: 改缓存实现**

把 `repo/header/utils/ConfigManager.h` 的 `getBlacklistFileNames()` 换成：
```cpp
    /// User-configured blacklist of exe filenames to hide from the switcher.
    /// In config.ini: [blacklist] \n file_names=app1.exe, app2.exe
    /// 结果被缓存：isWindowAcceptable() 每个窗口都会调它（Util.cpp:290），
    /// 动态扫描开启后这个调用会变得很频繁。configEdited 时失效。
    QStringList getBlacklistFileNames() {
        if (!blacklistCacheValid_) {
            auto raw = get("blacklist/file_names", QStringList{}).toStringList();
            QStringList result;
            for (auto& item: raw) {
                for (auto& sub: item.split(',')) {
                    auto trimmed = sub.trimmed();
                    if (!trimmed.isEmpty()) result << trimmed.toLower();
                }
            }
            blacklistCache_ = result;
            blacklistCacheValid_ = true;
        }
        return blacklistCache_;
    }
```

- [ ] **Step 2: 加成员与失效连接**

同文件的 `private:` 区（`explicit ConfigManager` 之前）加：
```cpp
    mutable QStringList blacklistCache_;
    mutable bool blacklistCacheValid_ = false;
```
构造函数改成：
```cpp
    explicit ConfigManager(const QString& filename) : ConfigManagerBase(filename) {
        // 配置文件经托盘 Settings 编辑保存后失效缓存
        connect(this, &ConfigManagerBase::configEdited, this, [this] {
            blacklistCacheValid_ = false;
        });
    }
```

- [ ] **Step 3: 构建**

Run: `rebuild.bat`
Expected: 构建成功。

- [ ] **Step 4: 交用户跑 —— 验证缓存会失效**

**不要自己声称已验证。** 两条都要，缺一不可：

1. **失效检查（这条才是关键）**：跑起 AltTaber → 托盘菜单 **Settings** → 在 `[blacklist] file_names=` 加一个当前正开着的程序（例如 `notepad.exe`）→ 保存关掉记事本窗口 → 按 Alt+Tab —— 预期该程序**从列表消失**。若不消失，说明失效逻辑没生效（缓存变成永久陈旧）。
2. **阳性对照**：把刚加的那条删掉再保存 → 按 Alt+Tab —— 预期它**重新出现**。

第 1 条正好是"把失效逻辑删掉它会红"的那个检查。

- [ ] **Step 5: 提交**

```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git add header/utils/ConfigManager.h
git commit -m "perf(config): cache parsed blacklist, invalidate on config reload"
```

---

### Task 4: 配置键 + `CONFIG.md` 合并

**Files:**
- Modify: `repo/header/utils/ConfigManager.h`（加两个 getter）
- Modify: `repo/CONFIG.md`、`repo/CONFIG_zh.md`（加 `[dynamic]` 节）
- Delete: `2026-06-16_alttaber_research/CONFIG.md`、`2026-06-16_alttaber_research/CONFIG_zh.md`

**Interfaces:**
- Produces: `bool ConfigManager::getWatchNewWindows()`、`bool ConfigManager::getAutoSelectNew()`

- [ ] **Step 1: 加两个 getter**

在 `repo/header/utils/ConfigManager.h` 的 `getBlacklistFileNames()` 之后加：
```cpp
    /// 切换器显示期间轮询新窗口并动态入列。
    /// In config.ini: [dynamic] \n watch_new_windows=true
    bool getWatchNewWindows() {
        return get("dynamic/watch_new_windows", false).toBool();
    }

    /// 新窗口入列时自动把选中项跳过去。开关 A 关闭时此项无效果。
    /// In config.ini: [dynamic] \n auto_select_new=true
    bool getAutoSelectNew() {
        return get("dynamic/auto_select_new", false).toBool();
    }
```

- [ ] **Step 2: 更新英文文档**

`repo/CONFIG.md`：在 `## Complete example` 的 ini 块里、`[sorting]` 之后加：
```ini
[dynamic]
watch_new_windows=true
auto_select_new=true
```
并在 `### `[sorting]` — Sort order` 一节之后插入新节：
```markdown
---

### `[dynamic]` — Live update while the switcher is open

Both default to `false`. `auto_select_new` has no effect while `watch_new_windows` is off.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `watch_new_windows` | bool | `false` | While the switcher is open, poll for newly appeared background windows at 300 ms and insert them into the list. Closed windows are removed symmetrically. |
| `auto_select_new` | bool | `false` | When a new window is inserted, move the selection onto it. Yields to manual navigation: once you press Tab / `` ` `` / an arrow / the wheel during the current popup, the selection stops being taken over until the next popup. |
```

- [ ] **Step 3: 更新中文文档**

`repo/CONFIG_zh.md`：在完整示例的 ini 块中加同样的 `[dynamic]` 段，并插入对应中文节：
```markdown
---

### `[dynamic]` — 切换器显示期间的实时更新

两项都默认 `false`。`auto_select_new` 在 `watch_new_windows` 关闭时无效果。

| 键 | 类型 | 默认 | 说明 |
|-----|------|---------|-------------|
| `watch_new_windows` | bool | `false` | 切换器显示期间以 300ms 轮询新出现的后台窗口并插入列表；窗口关闭时对称移除。 |
| `auto_select_new` | bool | `false` | 新窗口入列时把选中项移到它上面。会让位于手动操作：本次弹出期间一旦按过 Tab / `` ` `` / 方向键 / 滚轮，就不再抢选中，直到下次弹出。 |
```

- [ ] **Step 4: 合并重复的 CONFIG 副本（先验证 + 留后手，再删）**

⚠️ **父目录不是 git 仓库**（只有 `repo/` 是）。在那里删错文件**找不回来，而且不会报错**。

⚠️ **顺序陷阱**：Step 2/3 已经改过 `repo/CONFIG.md`，所以拿父目录副本去 `diff` 现在的 `repo/CONFIG.md` **必然不一致**，那条检查永远过不了。要比的是**提交前的原始内容**：

```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git show HEAD:CONFIG.md    | diff - ../CONFIG.md    && \
git show HEAD:CONFIG_zh.md | diff - ../CONFIG_zh.md && echo "IDENTICAL - safe to delete"
```
（`HEAD` 在未提交时是对的；若已经提交过，改用 `HEAD~1` 并在报告里说明。）

**留后手** —— 把两份先复制进本计划的工作区（成本为零，让误删可恢复）：
```bash
cp ../CONFIG.md ../CONFIG_zh.md ".superpowers/sdd/2026-09-30-group-window-list-and-dynamic-scan-plan/"
```

只有当上面输出 `IDENTICAL - safe to delete` 时才执行删除：
```bash
rm ../CONFIG.md ../CONFIG_zh.md
```
删完用 `ls` 证明文件确实不在了（**"命令返回 0" 不等于"文件没了"**）。`repo/` 下那两份保留为唯一来源。

- [ ] **Step 5: 构建并确认配置可读**

Run: `rebuild.bat`
Expected: 构建成功。

- [ ] **Step 6: 确认新键被读到且默认关闭（**非破坏性**）**

⚠️ **绝对不要删 `repo/build/Release/config.ini`。** 那是用户正在用的**真实配置**（里面有他的黑名单、字体等设置），而那个文件夹**不是 git 仓库**——删了就找不回来，而且**不会报错**。本步骤原来写的就是"删掉 config.ini"，那是个会静默毁掉用户设置的错误指令，已改。

改成非破坏性做法：

1. **只读检查**：`grep -c "^\[dynamic\]" repo/build/Release/config.ini` → 预期 **0**（默认值不写盘）。顺便 `cat` 一下确认文件内容完好、有你认识的那些设置。
2. **先备份**：`cp config.ini config.ini.bak`
3. **加键**：在末尾追加 `[dynamic]` 段 + `watch_new_windows=true` → 启动 AltTaber → 预期**不报错、不崩**（此时还没有消费者，Task 9 才接上）。
4. **还原**：用备份覆盖回去，并 `diff` 确认与原始逐字节一致。
5. 另外确认：`Alt+Tab` 行为与改动前完全一致（动态扫描默认关）。

- [ ] **Step 7: 提交**

```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git add CONFIG.md CONFIG_zh.md header/utils/ConfigManager.h
git commit -m "feat(config): add [dynamic] section for live window scanning"
```

---

### Task 5: 抽出 `WindowActivationOrder`（纯搬家，先搬后改、立刻验证）

用户已决定把 §偏差记录 里那条搬出来做。本 Task **只搬家，不改行为**——必须先把"旧行为一点没变"验证掉，后面的 Task 7-11 才敢依赖它。

**Files:**
- Create: `repo/header/utils/WindowActivationOrder.h`
- Create: `repo/src/utils/WindowActivationOrder.cpp`
- Modify: `repo/header/widget.h`（换掉成员、删掉 4 个私有方法声明）
- Modify: `repo/src/widget.cpp`（7 处调用点改为走新类）
- Modify: `repo/CMakeLists.txt`

**Interfaces:**
- Consumes: `Util::listValidWindows(const QString& exePath)`
- Produces:
  - `class WindowActivationOrder`（非 QObject，纯数据 + 查询）
  - `void record(const QString& exePath, HWND hwnd, const QDateTime& when)`
  - `QPair<HWND, QDateTime> lastActive(const QString& exePath) const`
  - `QPair<HWND, QDateTime> lastValidActive(const QString& exePath, const QList<HWND>& windows) const`
  - `void sortByActiveOrder(QList<HWND>& windows, const QString& exePath) const`
  - `QList<HWND> orderedWindowsOf(const QString& exePath) const`
  - `void clear()`

- [ ] **Step 1: 写头文件**

`repo/header/utils/WindowActivationOrder.h`：
```cpp
#ifndef WIN_SWITCHER_WINDOWACTIVATIONORDER_H
#define WIN_SWITCHER_WINDOWACTIVATIONORDER_H

#include <QString>
#include <QHash>
#include <QList>
#include <QDateTime>
#include <QPair>
#include <Windows.h>

/// 记录"每个进程的各个窗口最近一次成为前台的时间"，并提供按此排序的查询。
/// 这些逻辑原本是 Widget 的私有成员与私有方法，但 Alt+` 组列表、任务栏滚轮、
/// app 列表排序都要用，所以抽成独立单元。
class WindowActivationOrder {
public:
    /// 记录一次前台变化
    void record(const QString& exePath, HWND hwnd, const QDateTime& when);

    /// 该进程中最近活跃的窗口（不保证仍然有效）
    QPair<HWND, QDateTime> lastActive(const QString& exePath) const;

    /// windows 里最近活跃、且确实有记录的那个；没有则返回 {nullptr, QDateTime()}
    QPair<HWND, QDateTime> lastValidActive(const QString& exePath, const QList<HWND>& windows) const;

    /// 按活跃时间倒序重排 windows；该进程没有任何记录时**保持原顺序不动**
    void sortByActiveOrder(QList<HWND>& windows, const QString& exePath) const;

    /// 该进程当前所有有效窗口，按活跃时间倒序
    QList<HWND> orderedWindowsOf(const QString& exePath) const;

    void clear();

private:
    /// exePath -> (HWND, time)
    QHash<QString, QHash<HWND, QDateTime>> order_;
};

#endif //WIN_SWITCHER_WINDOWACTIVATIONORDER_H
```

- [ ] **Step 2: 写实现 —— 逐字搬，不许顺手改**

`repo/src/utils/WindowActivationOrder.cpp`：
```cpp
#include "utils/WindowActivationOrder.h"
#include "utils/Util.h"

void WindowActivationOrder::record(const QString& exePath, HWND hwnd, const QDateTime& when) {
    order_[exePath].insert(hwnd, when);
}

QPair<HWND, QDateTime> WindowActivationOrder::lastActive(const QString& exePath) const {
    auto hwndOrder = order_.value(exePath);
    if (hwndOrder.isEmpty()) return {nullptr, QDateTime()};
    // QHash & QMap deref to value(QDateTime) rather than QPair
    auto iter = std::max_element(hwndOrder.begin(), hwndOrder.end());
    return {iter.key(), iter.value()};
}

QPair<HWND, QDateTime> WindowActivationOrder::lastValidActive(const QString& exePath,
                                                              const QList<HWND>& windows) const {
    auto hwndOrder = order_.value(exePath);
    if (hwndOrder.isEmpty()) return {nullptr, QDateTime()};

    QList<HWND> ordered = windows;
    sortByActiveOrder(ordered, exePath);

    if (auto time = hwndOrder.value(ordered.first()); !time.isNull())
        return {ordered.first(), time};
    else // check if the first HWND is recorded
        return {nullptr, QDateTime()};
}

/// sort Windows of [Group specified by exePath], by active order (latest first)
void WindowActivationOrder::sortByActiveOrder(QList<HWND>& windows, const QString& exePath) const {
    auto activeOrdMap = order_.value(exePath);
    if (activeOrdMap.isEmpty()) return;
    // sort by active order
    std::sort(windows.begin(), windows.end(), [&activeOrdMap](HWND a, HWND b) {
        return activeOrdMap.value(a) > activeOrdMap.value(b); // default value if not found
    }); // TODO update winActiveOrder! (remove invalid HWND)
}

/// group by exePath, sort by active order (last active first)
QList<HWND> WindowActivationOrder::orderedWindowsOf(const QString& exePath) const {
    auto windows = Util::listValidWindows(exePath); // filter by path
    sortByActiveOrder(windows, exePath);
    return windows;
}

void WindowActivationOrder::clear() {
    order_.clear();
}
```
**两处必须逐字保留的旧行为**（改掉任何一处都是行为变更，不是重构）：
1. `sortByActiveOrder()` 里 `if (activeOrdMap.isEmpty()) return;` 的**提前返回**——该进程没有任何记录时，调用方传进来的窗口顺序**原封不动**（即保持枚举顺序），不是"按空 map 排序"。
2. `lastValidActive()` 里判断的是**排序后第一个**窗口有没有记录，而不是"找一个有记录的"。这个语义有点绕，但它就是现状。

**一个既有的隐患，本次不要修**：`lastValidActive()` 在 `windows` 为空时会走到 `ordered.first()`（空 QList 取首元素）。这是搬家前就存在的，调用方目前都保证非空。**发现即可，写进报告，不要顺手加保护**——那是行为变更，会把"纯搬家"的 diff 弄脏。

- [ ] **Step 3: 改 `widget.h`**

- 删掉成员 `QHash<QString, QHash<HWND, QDateTime>> winActiveOrder;` 及其注释 `/// exePath -> (HWND, time)`，换成 `WindowActivationOrder winActiveOrder_;`
- 删掉这 4 个私有方法声明：`getLastActiveGroupWindow` / `getLastValidActiveGroupWindow` / `sortGroupWindows` / `buildGroupWindowOrder`
- 加 `#include "utils/WindowActivationOrder.h"`

**保留** `QList<HWND> groupWindowOrder;`（任务栏滚轮与 Alt+` 仍在用，Task 7 才会改）。

- [ ] **Step 4: 改 `widget.cpp` 的 7 处调用点**

| 位置 | 原样 | 改成 |
|---|---|---|
| `notifyForegroundChanged()` | `winActiveOrder[path].insert(hwnd, QDateTime::currentDateTime());` | `winActiveOrder_.record(path, hwnd, QDateTime::currentDateTime());` |
| `keyReleaseEvent()` | `getLastActiveGroupWindow(group.exePath).first` | `winActiveOrder_.lastActive(group.exePath).first` |
| `prepareWindowGroupList()`（`new_app_second` 分支：**3 个调用点**，分布在 2 行） | `getLastValidActiveGroupWindow(group)` ×1、`(a)` / `(b)` ×2 | `winActiveOrder_.lastValidActive(group.exePath, hwndsOfGroup(group))` 等 |
| `prepareWindowGroupList()`（else 分支：2 个调用点） | `getLastValidActiveGroupWindow(a)` / `(b)` | 同上写法 |
| `eventFilter()` | `buildGroupWindowOrder(targetExe)` | `winActiveOrder_.orderedWindowsOf(targetExe)` |
| `keyPressEvent()` 的 Alt+` 盲切分支 | `buildGroupWindowOrder(targetExe)` | `winActiveOrder_.orderedWindowsOf(targetExe)` |
| `rotateTaskbarWindowInGroup()`（3 处） | `buildGroupWindowOrder(...)` | `winActiveOrder_.orderedWindowsOf(...)` |

`getLastValidActiveGroupWindow(const WindowGroup&)` 原本是从 `group.windows` 里取 hwnd，所以在 `widget.cpp` 里加一个文件内的静态小工具（放在 `namespace { }` 里，或 `Widget` 的私有 inline 方法）：
```cpp
/// 从 WindowGroup 里取出 hwnd 列表，顺序与 group.windows 一致
static QList<HWND> hwndsOfGroup(const WindowGroup& group) {
    QList<HWND> hwnds;
    for (auto& info: group.windows) hwnds << info.hwnd;
    return hwnds;
}
```

然后**删掉** `widget.cpp` 里 `getLastActiveGroupWindow` / `getLastValidActiveGroupWindow` / `sortGroupWindows` / `buildGroupWindowOrder` 这四个函数的定义（连同它们的注释与 TODO——TODO 已在 Step 2 搬进新实现）。

- [ ] **Step 5: 加进 CMake**

在 `Win_Switcher` 源文件列表里、`header/utils/ConfigManagerBase.h` 那行之后插入：
```cmake
        src/utils/WindowActivationOrder.cpp
        header/utils/WindowActivationOrder.h
```

- [ ] **Step 6: 构建**

Run: `rebuild.bat`
Expected: 构建成功，**没有任何新警告**（尤其不能有"未使用变量/函数"）。

- [ ] **Step 7: 自证是"纯搬家"**

Run:
```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git diff -U0 -- src/widget.cpp
```
逐行看这段 diff：**每一处删除都应该在 `WindowActivationOrder.cpp` 里找到逐字对应的新增**，除了调用点的名字替换。任何一处逻辑被改写、参数被调整、条件被"优化"，都要停下来报告。把结论写进报告（"7 处调用点全部为机械替换，无逻辑变更" 或具体指出哪一处不是）。

- [ ] **Step 8: 交用户跑 —— 先搬后改的验证关口**

**不要自己声称已验证。** 这一步是本次改动里唯一一次动到既有三条独立链路的时刻，必须逐条确认行为**与改动前完全一致**：

- [ ] **Alt+Tab**：唤出切换器、连按走位、松手切到正确窗口——排序与之前一致
- [ ] **Alt+`（盲切）**：此时还是旧的无界面行为，连按应在同组窗口间轮换、Shift 反向
- [ ] **任务栏滚轮**：在任务栏图标上滚——同组窗口前后切换、向下滚最小化，均与之前一致
- [ ] **黑名单**：配置里的程序仍不出现在列表
- [ ] **`new_app_second`**：开启时无使用记录的应用仍排第 2 位

任何一条不一致都说明"搬家"搬坏了，必须回查 Step 2/Step 4。

- [ ] **Step 9: 提交**

```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git add header/utils/WindowActivationOrder.h src/utils/WindowActivationOrder.cpp header/widget.h src/widget.cpp CMakeLists.txt
git commit -m "refactor: extract WindowActivationOrder out of Widget"
```

---

### Task 6: `GroupWindowList`（竖排列表的展示与选中）

只做展示与选中，**不管排序**（排序仍由 `Widget` 提供，见文末 §偏差记录）。为避免头文件循环依赖，先把 `WindowInfo` 从 `widget.h` 抽出来。

**Files:**
- Create: `repo/header/utils/WindowInfo.h`
- Create: `repo/header/utils/GroupWindowList.h`
- Create: `repo/src/utils/GroupWindowList.cpp`
- Modify: `repo/header/widget.h`（删掉 `WindowInfo`，改为 include 新头）
- Modify: `repo/CMakeLists.txt`

**Interfaces:**
- Consumes: `WindowInfo`
- Produces:
  - `class GroupWindowList`（非 QObject，纯 C++ 类）
  - `explicit GroupWindowList(QWidget* parent)`；`QListWidget* widget()`
  - `void fill(const QList<WindowInfo>&)` —— 按传入顺序填行，调用方保证已按最后使用时间降序
  - `void clear()`；`int count() const`
  - `int currentRow() const`；`void setCurrentRow(int)`；`void moveSelection(int delta)`（端点循环）
  - `HWND currentHwnd() const`（无选中返回 `nullptr`）
  - `int append(const WindowInfo&)` → 返回新行下标
  - `int removeByHwnd(HWND)` → 返回被移除的下标，未找到返回 `-1`
  - `QSize preferredSize() const`
  - `static constexpr int RowHeight = 28, MinWidth = 220, MaxWidth = 480, MaxVisibleRows = 8;`

- [ ] **Step 1: 抽出 `WindowInfo`**

新建 `repo/header/utils/WindowInfo.h`，把 `widget.h:11-23` 的结构体定义原样搬过来：
```cpp
#ifndef WIN_SWITCHER_WINDOWINFO_H
#define WIN_SWITCHER_WINDOWINFO_H

#include <QString>
#include <QDebug>
#include <Windows.h>

struct WindowInfo {
    QString title;
    QString className;
    HWND hwnd = nullptr;
};

// C++17 inline: 防止重定义
inline QDebug operator<<(QDebug dbg, const WindowInfo& info) {
    dbg.nospace() << "WindowInfo(" << info.title << ", " << info.className << ", " << info.hwnd << ")";
    return dbg.space();
}

Q_DECLARE_METATYPE(WindowInfo) // for QVariant

#endif //WIN_SWITCHER_WINDOWINFO_H
```
然后 `repo/header/widget.h` 里删掉这段（含 `operator<<` 与 `Q_DECLARE_METATYPE`），改成 `#include "utils/WindowInfo.h"`。`struct WindowGroup` 留在 `widget.h` 不动。

- [ ] **Step 2: 写 `GroupWindowList` 头文件**

```cpp
#ifndef WIN_SWITCHER_GROUPWINDOWLIST_H
#define WIN_SWITCHER_GROUPWINDOWLIST_H

#include <QListWidget>
#include <QList>
#include <QSize>
#include <Windows.h>
#include "utils/WindowInfo.h"

/// Alt+` 的竖排窗口列表：只管展示与选中，不管排序。
/// 调用方负责把窗口按最后使用时间降序排好再传进来。
class GroupWindowList {
public:
    static constexpr int RowHeight = 28;      // 每行高度
    static constexpr int MinWidth = 220;      // 宽度下限
    static constexpr int MaxWidth = 480;      // 宽度上限
    static constexpr int MaxVisibleRows = 8;  // 超过则内部滚动
    static constexpr int PaddingH = 24;       // 水平内边距（左右各 12）
    static constexpr int PaddingV = 16;       // 垂直内边距

    explicit GroupWindowList(QWidget* parent);

    QListWidget* widget() { return list_; }
    void fill(const QList<WindowInfo>& windows);
    void clear();
    int count() const;
    int currentRow() const;
    void setCurrentRow(int row);
    void moveSelection(int delta);
    HWND currentHwnd() const;
    int append(const WindowInfo& info);
    int removeByHwnd(HWND hwnd);
    QSize preferredSize() const;

private:
    /// 按当前内容重算宽度并应用到列表与每一行（fill / append / removeByHwnd 后都要调）
    void applyPreferredWidth();
    QListWidget* list_ = nullptr;
};

#endif //WIN_SWITCHER_GROUPWINDOWLIST_H
```

- [ ] **Step 3: 写实现**

```cpp
#include "utils/GroupWindowList.h"
#include <QFontMetrics>

GroupWindowList::GroupWindowList(QWidget* parent) {
    list_ = new QListWidget(parent);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setFocusPolicy(Qt::NoFocus);           // 按键由 Widget 统一处理，列表不抢焦点
    list_->setUniformItemSizes(true);
    list_->setTextElideMode(Qt::ElideRight);      // 过长标题右省略
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setStyleSheet(R"(
        QListWidget {
            background-color: transparent;
            border: none;
            outline: none;
        }
        QListWidget::item {
            color: rgb(240, 240, 240);
            padding-left: 12px;
            padding-right: 12px;
        }
        QListWidget::item:selected {
            background-color: rgba(255, 255, 255, 38);
            border-radius: 4px;
        }
    )");
    list_->hide();
}

void GroupWindowList::clear() {
    list_->clear();
}

int GroupWindowList::count() const {
    return list_->count();
}

int GroupWindowList::currentRow() const {
    return list_->currentRow();
}

void GroupWindowList::setCurrentRow(int row) {
    if (list_->count() == 0) return;
    list_->setCurrentRow(qBound(0, row, list_->count() - 1));
}

void GroupWindowList::moveSelection(int delta) {
    const int n = list_->count();
    if (n == 0) return;
    // 端点循环
    list_->setCurrentRow(((list_->currentRow() + delta) % n + n) % n);
}

HWND GroupWindowList::currentHwnd() const {
    auto* item = list_->currentItem();
    if (!item) return nullptr;
    return item->data(Qt::UserRole).value<WindowInfo>().hwnd;
}

int GroupWindowList::append(const WindowInfo& info) {
    auto* item = new QListWidgetItem(info.title);
    item->setData(Qt::UserRole, QVariant::fromValue(info));
    item->setSizeHint(QSize(MinWidth, RowHeight));
    list_->addItem(item);
    applyPreferredWidth();   // 新行可能更长，宽度要跟着变
    return list_->count() - 1;
}

int GroupWindowList::removeByHwnd(HWND hwnd) {
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(Qt::UserRole).value<WindowInfo>().hwnd == hwnd) {
            delete list_->takeItem(i);
            applyPreferredWidth();
            return i;
        }
    }
    return -1;
}

QSize GroupWindowList::preferredSize() const {
    // 宽度按最长标题算，clamp 到 [MinWidth, MaxWidth]
    int textWidth = 0;
    for (int i = 0; i < list_->count(); ++i)
        textWidth = qMax(textWidth, list_->fontMetrics().horizontalAdvance(list_->item(i)->text()));
    const int width = qBound(MinWidth, textWidth + PaddingH, MaxWidth);
    const int rows = qBound(1, list_->count(), MaxVisibleRows);
    return {width, rows * RowHeight + PaddingV};
}

void GroupWindowList::applyPreferredWidth() {
    const int width = preferredSize().width();
    for (int i = 0; i < list_->count(); ++i)
        list_->item(i)->setSizeHint(QSize(width, RowHeight));
    list_->setFixedWidth(width);
}

void GroupWindowList::fill(const QList<WindowInfo>& windows) {
    list_->clear();
    for (auto& info: windows) {
        auto* item = new QListWidgetItem(info.title);
        item->setData(Qt::UserRole, QVariant::fromValue(info));
        item->setSizeHint(QSize(MinWidth, RowHeight));   // 宽度先占位，下面统一重设
        list_->addItem(item);
    }
    applyPreferredWidth();
}
```

**为什么用 `applyPreferredWidth()` 而不是让 `append()` 直接算**：新行的标题可能比现有行都长，宽度必须整体重算并同步到**每一行**的 sizeHint（只是 `setFixedWidth` 列表而不改行的 sizeHint，会出现行宽与列表宽不一致，选中高亮的圆角背景就会画得忽长忽短）。
注意：`preferredSize()` 里的 `list_->fontMetrics()` 在 `list_` 不可见时也能用（它取的是 list 的字体度量，不依赖绘制）。若实践下来发现行高与文字高不匹配，`RowHeight` 是唯一该调的地方。

- [ ] **Step 4: 加进 CMake**

在 `Win_Switcher` 源文件列表里，`header/utils/ConfigManagerBase.h` 那行之后插入：
```cmake
        src/utils/GroupWindowList.cpp
        header/utils/GroupWindowList.h
        header/utils/WindowInfo.h
```

- [ ] **Step 5: 构建**

Run: `rebuild.bat`
Expected: 构建成功，现有行为零变化（这个类还没有被任何人使用）。

- [ ] **Step 6: 交用户跑 —— 回归确认**

**不要自己声称已验证。** 交给用户：跑 Alt+Tab / Alt+` 盲切 / 任务栏滚轮 —— 三者行为应与改动前**完全一致**（本 Task 只是加了新类 + 把 `WindowInfo` 换了个文件，不应有任何可观测变化）。

- [ ] **Step 7: 提交**

```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git add header/utils/WindowInfo.h header/utils/GroupWindowList.h src/utils/GroupWindowList.cpp header/widget.h CMakeLists.txt
git commit -m "refactor: extract WindowInfo header, add GroupWindowList widget"
```

---

### Task 7: `Mode` + 入口 A（独立 Alt+` 弹出列表）

**Files:**
- Modify: `repo/header/widget.h`
- Modify: `repo/src/widget.cpp`

**Interfaces:**
- Consumes: `GroupWindowList`（Task 6）、既有的 `orderedWindowsOf()` / `rotateWindowInGroup()` / `lastActive()`
- Produces:
  - `enum class Mode { AppMode, GroupMode };`（`Widget` 的 public 嵌套类型）
  - `Widget::mode_`（private）、`Widget::groupList_`、`Widget::userNavigated_`
  - `Q_INVOKABLE bool requestShowGroup();`
  - `void Widget::enterGroupMode(const QString& exePath, int initialRow);`（Task 8 复用）
  - `void Widget::layoutForCurrentMode();`

- [ ] **Step 1: 头文件加成员与声明**

`repo/header/widget.h` 里：
```cpp
public:
    enum class Mode { AppMode, GroupMode };
```
public 区加：
```cpp
    Q_INVOKABLE bool requestShowGroup();
```
private 区加：
```cpp
    /// 进入 GroupMode：用给定进程的窗口填充竖排列表，并定好初始高亮
    void enterGroupMode(const QString& exePath, int initialRow);
    /// Alt 抬起时，GroupMode 的提交动作
    void commitGroupSelection();
    /// 本程序**唯一**的窗口几何计算处：按当前 mode_ 决定内容尺寸，
    /// 再把窗口居中到目标屏幕。返回 false 表示取不到目标屏幕
    bool layoutForCurrentMode();
    /// 按 DisplayMonitor 配置挑选目标屏幕（失败回退主屏）。原本内联在 prepareListWidget() 里，
    /// 抽出来给两个模式共用，避免复制一份屏幕选择逻辑
    QScreen* targetScreen();
    /// 该进程的窗口，按最后使用时间降序
    QList<WindowInfo> orderedGroupWindows(const QString& exePath);
```
成员区加：
```cpp
    Mode mode_ = Mode::AppMode;
    GroupWindowList* groupList_ = nullptr;   // 在构造函数里 new，parent 为 this
    bool userNavigated_ = false;             // 本次弹出期间用户是否手动翻过列表
```
并 `#include "utils/GroupWindowList.h"`。

- [ ] **Step 2: 构造函数里建列表，并装滚轮事件过滤器**

`repo/src/widget.cpp` 构造函数末尾（`connect(qApp, &QApplication::focusWindowChanged, ...)` 之后）加：
```cpp
    groupList_ = new GroupWindowList(this);
    groupList_->widget()->installEventFilter(this);   // 滚轮移动高亮（见 Step 2b）
```

- [ ] **Step 2b: 在 `eventFilter` 里加组列表的滚轮分支**

**必须加在既有 `watched == lw` 那个分支之前**，并且只在自己处理时才返回 `true`。`repo/src/widget.cpp` 的 `eventFilter()` 开头插入：
```cpp
    if (mode_ == Mode::GroupMode && watched == groupList_->widget() && event->type() == QEvent::Wheel) {
        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        // 向上滚 = 上移
        groupList_->moveSelection(wheelEvent->angleDelta().y() > 0 ? -1 : +1);
        userNavigated_ = true;
        return true;
    }
```
说明：既有的滚轮分支（`widget.cpp:422-473`）只处理 `lw`，且里面有一整套针对"滚轮切换同组窗口 + 偷鸡隐藏 thumbnail"的逻辑，**不要动它**，也不要让两个模式共用同一个分支。

- [ ] **Step 3: 实现 `orderedGroupWindows()`**

```cpp
/// 该进程的窗口，按最后使用时间降序（复用 Task 5 抽出的排序）
QList<WindowInfo> Widget::orderedGroupWindows(const QString& exePath) {
    QList<WindowInfo> result;
    for (auto hwnd: winActiveOrder_.orderedWindowsOf(exePath))
        result.append({Util::getWindowTitle(hwnd), Util::getClassName(hwnd), hwnd});
    return result;
}
```

- [ ] **Step 4: 实现 `enterGroupMode()`**

```cpp
void Widget::enterGroupMode(const QString& exePath, int initialRow) {
    if (exePath.isEmpty()) return;
    auto windows = orderedGroupWindows(exePath);
    if (windows.isEmpty()) return;   // 没有窗口就不弹空列表

    mode_ = Mode::GroupMode;
    userNavigated_ = false;
    groupList_->fill(windows);
    groupList_->setCurrentRow(initialRow);
    if (!layoutForCurrentMode()) {
        mode_ = Mode::AppMode;       // 取不到屏幕就退回，不留半吊子状态
        groupList_->clear();
    }
}
```
（`layoutForCurrentMode()` 的完整实现见 Step 4b。）

- [ ] **Step 4b: 实现 `targetScreen()` 与 `layoutForCurrentMode()`，并让 `prepareListWidget()` 复用**

**为什么把这段单列**：几何居中（算内容尺寸 → 加边距 → 居中到屏幕 → setGeometry → 把内容移到窗口中央）如果 AppMode 和 GroupMode 各写一份，就是评审会**直接判为缺陷**的复制粘贴。所以这里把它收敛成**唯一**一处 `layoutForCurrentMode()`，并让既有的 `prepareListWidget()` 改为调用它。

`repo/src/widget.cpp` 新增两个函数：
```cpp
/// 按 DisplayMonitor 配置挑选目标屏幕；配置为跟随鼠标但取不到光标下屏幕时回退主屏
QScreen* Widget::targetScreen() {
    const bool displayOnPrimary = (cfg.getDisplayMonitor() == PrimaryMonitor);
    auto* screen = displayOnPrimary ? QGuiApplication::primaryScreen()
                                    : QGuiApplication::screenAt(QCursor::pos());
    if (!screen && !displayOnPrimary) { // fallback to primary screen
        qWarning() << "Cursor Screen nullptr! Fallback to primary";
        screen = QGuiApplication::primaryScreen();
    }
    return screen;
}

/// 本程序唯一的窗口几何计算处：按当前 mode_ 决定内容尺寸，再把窗口居中到目标屏幕
bool Widget::layoutForCurrentMode() {
    QWidget* content = nullptr;
    if (mode_ == Mode::AppMode) {
        groupList_->widget()->hide();
        lw->show();
        ui->label->show();
        content = lw;   // 宽度已由 prepareListWidget() 里的 setFixedWidth 设好
    } else {
        lw->hide();
        ui->label->hide();
        auto* view = groupList_->widget();
        view->setFixedSize(groupList_->preferredSize());
        view->show();
        content = view;
    }

    auto* screen = targetScreen();
    if (!screen) {
        qWarning() << "Screen nullptr!";
        sysTray.showMessage("Error", "Screen nullptr!");
        return false;
    }

    //region Fixed in Qt6, see commit [b927ee4b]
    //      !!!WARNING: 对于多屏幕，直接使用setGeometry or move会报错(QWindowsWindow::setGeometry: Unable to set geometry) & size显示不正确！
    //      报错时机为：从一个屏幕hide，再在另一个屏幕show; 第二次在同一个屏幕show，则正常
    //      size显示不正确不能忍，遂改用WinAPI
    //endregion
    qDebug() << "Screen:" << screen->name();
    auto contentRect = QRect(QPoint(0, 0), content->size());
    auto thisRect = contentRect.marginsAdded(ListWidgetMargin);
    thisRect.moveCenter(screen->geometry().center());
    this->setGeometry(thisRect);

    contentRect.moveCenter(this->rect().center()); // local pos
    content->move(contentRect.topLeft());
    return true;
}
```

然后修改 `prepareListWidget()`：把 `widget.cpp:303-345` 这一段（从 `// get screen` 到 `lw->move(lwRect.topLeft());`，含中间那一大段关于 `setGeometry` vs `move/resize` 的注释）**整段删除**，替换为：
```cpp
        // 几何：本程序唯一的居中逻辑在 layoutForCurrentMode()（Step 4b）
        if (!layoutForCurrentMode())
            return false;
```
**保留**它上面的 `lw->setFixedWidth(width);`（宽度必须先由 `visualItemRect` 算出来，`layoutForCurrentMode()` 直接用 `lw` 的现成尺寸），**保留**它下面的"set current item"整块。

行为必须**逐字保持**：`displayOnPrimary` 分支、`screenAt(QCursor::pos())`、两条回退、两条 warning（`"Cursor Screen nullptr! Fallback to primary"` 现在在 `targetScreen()` 里，`"Screen nullptr!"` + `sysTray.showMessage` 在 `layoutForCurrentMode()` 里）、以及 `qDebug() << "Screen:" << screen->name();` 那行。

- [ ] **Step 5: 实现 `requestShowGroup()`（入口 A）**

```cpp
/// 入口 A：Alt+` 独立按下（切换器不可见）——列表用「前台窗口所在进程」的窗口
bool Widget::requestShowGroup() {
    auto foreWin = GetForegroundWindow();
    auto exePath = Util::getWindowProcessPath(foreWin);
    if (exePath.isEmpty()) return false;

    auto order = winActiveOrder_.orderedWindowsOf(exePath);
    if (order.isEmpty()) return false;

    // 初始高亮 = 旧盲切的目标：前台窗口下标 +1（Shift 则 -1），端点循环。
    // 用下标算，不假设"前台窗口一定排首行"——那样高亮会偏。
    const int n = order.size();
    int idx = order.indexOf(foreWin);
    if (idx < 0) idx = 0;                       // 前台窗口不在过滤后的列表里，退化为首行
    const bool backward = Util::isKeyPressed(VK_SHIFT);
    const int delta = backward ? -1 : 1;
    const int initialRow = ((idx + delta) % n + n) % n;

    enterGroupMode(exePath, initialRow);
    return this->isVisible();
}
```

- [ ] **Step 6: 改 `keyPressEvent` 的 Alt+` 分支**

把 `repo/src/widget.cpp:91-106` 整段（原来的盲切实现）替换成：
```cpp
    } else if (key == Qt::Key_QuoteLeft && (modifiers & Qt::AltModifier)) {
        const int delta = (modifiers & Qt::ShiftModifier) ? -1 : 1;
        if (mode_ == Mode::GroupMode && this->isVisible()) {
            // 情形 C：已在组列表内 —— 移动高亮
            groupList_->moveSelection(delta);
            userNavigated_ = true;
        } else {
            // 情形 A（不可见）/ 情形 B（AppMode 可见，由 Task 8 接管）
            requestShowGroup();
        }
        return;
    }
```
注意：原来的 `groupWindowOrder.clear()`（`widget.cpp:182`，Alt 抬起时）**保留不动**，任务栏滚轮链路仍依赖它。

- [ ] **Step 7: 改 `keyReleaseEvent` 的提交分支**

在 `keyReleaseEvent` 里 `if (event->key() == Qt::Key_Alt)` 块的开头插入：
```cpp
        if (mode_ == Mode::GroupMode) {
            commitGroupSelection();
            groupWindowOrder.clear();      // 与 AppMode 一致，Alt 抬起即清
            mode_ = Mode::AppMode;
            groupList_->clear();
            hide();
            QWidget::keyReleaseEvent(event);
            return;
        }
```
并实现：
```cpp
void Widget::commitGroupSelection() {
    if (auto hwnd = groupList_->currentHwnd()) {
        Util::switchToWindow(hwnd);
        qInfo() << "(Alt+`)Switch to" << Util::getWindowTitle(hwnd) << Util::getClassName(hwnd);
    }
}
```
`hide()` 必须发生在 `switchToWindow` **之后**，与既有 AppMode 的提交一致（`widget.cpp:201` 的注释解释了原因：否则焦点会回落到上一个前台窗口）。

- [ ] **Step 8: `requestShow()` 里重置 AppMode 状态**

`requestShow()` 开头加 `mode_ = Mode::AppMode; userNavigated_ = false;`。

不需要再额外调用 `layoutForCurrentMode()`——`prepareListWidget()` 内部已经会调（见 Step 4b），它会顺带把两个列表的显隐切干净（`lw` 显示、`groupList_` 隐藏），这正是从 GroupMode 回到 AppMode 时需要的。

- [ ] **Step 9: 构建**

Run: `rebuild.bat`
Expected: 构建成功。

- [ ] **Step 10: 交用户跑 —— 入口 A 的完整交互**

**不要自己声称已验证。** 交给用户，逐条打勾：

- [ ] 让某个有多窗口的程序在前台（例如开着 3 个窗口的记事本/浏览器），按一下 Alt+` 立即松手 —— 预期**切到旧盲切的同一个窗口**（与改动前对比验证）
- [ ] 再按一次 Alt+`，**按住不动** —— 预期弹出玻璃背景的竖排列表，标题按最近使用降序，当前窗口在第一行，**高亮在第二行**
- [ ] 连按 ` 两下 —— 预期高亮下移两行；到底后**循环**回第一行
- [ ] 按 Shift+` —— 预期高亮上移；到顶后循环到最后一行
- [ ] 按住 Alt 时按 ↑ ↓ ← → 和 H J K L —— 预期都是上下移动
- [ ] 松开 Alt —— 预期切到高亮的那个窗口，列表消失
- [ ] 窗口标题超长时 —— 预期右省略（`…`），没有横向滚动条
- [ ] 单窗口的程序按 Alt+` —— 预期弹出一行的列表，松手无变化
- [ ] 用 Task 2 的临时脚手架观察 —— 此时按下 Alt+` **不应**有任何 `[watch] new windows` 刷屏

- [ ] **Step 11: 提交**

```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git add header/widget.h src/widget.cpp
git commit -m "feat(altgrave): show vertical group window list instead of blind rotate"
```

---

### Task 8: 入口 B（从切换器进入组列表）

**Files:**
- Modify: `repo/src/widget.cpp`（`requestShowGroup()`）

**Interfaces:**
- Consumes: `Widget::enterGroupMode()`（Task 7）、`WindowGroup`（`widget.h`）
- Produces: `requestShowGroup()` 支持"从 AppMode 可见状态进入"，初始高亮为**第 0 行**

- [ ] **Step 1: 在 `requestShowGroup()` 里加入口 B 分支**

在 `requestShowGroup()` 的开头（取 `foreWin` 之前）插入：
```cpp
    // 入口 B：AppMode 可见时按 ` —— 用「当前选中项」所在进程，而不是前台窗口
    // （此时前台窗口是本 Widget 自己，取它没有意义）
    if (mode_ == Mode::AppMode && this->isVisible()) {
        auto* cur = lw->currentItem();
        if (!cur) return false;
        auto exePath = cur->data(Qt::UserRole).value<WindowGroup>().exePath;
        if (exePath.isEmpty()) return false;
        // 初始高亮第 0 行 = 该组最后使用的窗口 = 不按 ` 直接松手会切到的那个。
        // 保持这条不变式：按 ` 再松手 == 什么都不做的松手。
        // 必须用返回值：丢掉它会让"陈旧 GroupMode"那一类 bug 复活（Task 7 的修复轮刚堵掉）
        if (!enterGroupMode(exePath, 0)) return false;
        return this->isVisible();
    }
```

- [ ] **Step 2: 构建**

Run: `rebuild.bat`
Expected: 构建成功。

- [ ] **Step 3: 交用户跑 —— 入口 B 与不变式**

**不要自己声称已验证。** 交给用户：

- [ ] 按 Alt+Tab 唤出切换器，把高亮移到某个**不是**前台的应用（例如移到第 3 个图标），按 ` —— 预期弹出**那个应用**的窗口竖排列表（不是前台应用的）
- [ ] **不变式检查**：重复上一步，但按下 ` 后**立刻松手** —— 预期结果与"直接松手不按 `"**完全一样**（都切到那个应用最近使用的窗口）
- [ ] 在组列表里连按 ` 再松手 —— 预期切到更早使用的那个窗口
- [ ] 高亮停在**第一个**图标上按 ` —— 预期也正常弹出该应用的列表
- [ ] 在组列表状态下松开 Alt —— 预期切窗口、列表消失，且**下一次 Alt+Tab 正常回到 app 列表模式**（不是还停在组列表）

- [ ] **Step 4: 提交**

```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git add src/widget.cpp
git commit -m "feat(altgrave): enter group list from the selected app in the switcher"
```

---

### Task 9: AppMode 动态扫描（入列 / 移除 / 手动优先 / 自动选中）

**Files:**
- Modify: `repo/src/widget.cpp`（新增连接与处理函数）
- Modify: `repo/header/widget.h`（成员与声明）
- Modify: `repo/src/main.cpp`（**删掉 Task 2 的临时脚手架**）

**Interfaces:**
- Consumes: `WindowWatcher`（Task 2）、`cfg.getWatchNewWindows()` / `cfg.getAutoSelectNew()`（Task 4）
- Produces:
  - `Widget::watcher_`（`WindowWatcher*`）
  - `void Widget::onNewWindows(const QList<HWND>&)` / `void Widget::onWindowsGone(const QList<HWND>&)`
  - `void Widget::insertAppItem(const QString& exePath, HWND hwnd)`
  - `void Widget::removeEmptyGroups()`

- [ ] **Step 1: 成员与声明**

`repo/header/widget.h` private 区加：
```cpp
    void onNewWindows(const QList<HWND>& added);
    void onWindowsGone(const QList<HWND>& removed);
    /// AppMode：为新进程插入一个列表项（沿用既有排序语义：无使用记录 → 末尾）
    int insertAppItem(const QString& exePath, HWND hwnd);
    /// 移除窗口已全部关闭的组
    void removeEmptyGroups();
```
成员加 `WindowWatcher* watcher_ = nullptr;` 并 include 其头文件。

- [ ] **Step 2: 构造里创建并连接**

```cpp
    watcher_ = new WindowWatcher(this);
    connect(watcher_, &WindowWatcher::newWindowsFound, this, &Widget::onNewWindows);
    connect(watcher_, &WindowWatcher::windowsGone, this, &Widget::onWindowsGone);
```

- [ ] **Step 3: 在显示/隐藏时启停**

`requestShow()` 成功显示后：
```cpp
    if (cfg.getWatchNewWindows()) watcher_->start();
    else watcher_->stop();
```
`hide()` 的所有路径都要停。在 `Widget` 里加一个覆写：
```cpp
void Widget::hideEvent(QHideEvent* event) {
    watcher_->stop();
    QWidget::hideEvent(event);
}
```
（`repo/header/widget.h` 的 protected 区加 `void hideEvent(QHideEvent* event) override;`，并在 `widget.cpp` 里 include `<QHideEvent>`。）

- [ ] **Step 4: 实现 AppMode 的入列与移除**

```cpp
void Widget::onNewWindows(const QList<HWND>& added) {
    if (mode_ != Mode::AppMode || !this->isVisible()) return;

    QList<QListWidgetItem*> newlyAdded;
    for (auto hwnd: added) {
        auto path = Util::getWindowProcessPath(hwnd);   // 只为新窗口查，慢操作
        if (path.isEmpty()) continue;

        // 找已有的同进程项
        QListWidgetItem* existing = nullptr;
        for (int i = 0; i < lw->count(); ++i) {
            if (lw->item(i)->data(Qt::UserRole).value<WindowGroup>().exePath == path) {
                existing = lw->item(i);
                break;
            }
        }

        if (existing) {
            // 已有进程多了一个窗口：不新增项，只把窗口并进该组的窗口列表
            auto group = existing->data(Qt::UserRole).value<WindowGroup>();
            group.addWindow({Util::getWindowTitle(hwnd), Util::getClassName(hwnd), hwnd});
            existing->setData(Qt::UserRole, QVariant::fromValue(group));
        } else {
            int row = insertAppItem(path, hwnd);
            if (row >= 0) newlyAdded.append(lw->item(row));
        }
    }

    if (newlyAdded.isEmpty()) return;
    // 开关 B + 手动优先：本次弹出期间用户翻过列表就不再抢
    if (cfg.getAutoSelectNew() && !userNavigated_)
        lw->setCurrentItem(newlyAdded.last());
}

/// 沿用既有排序语义：无使用记录的新进程排到末尾；new_app_second 开启时插到第 2 位
int Widget::insertAppItem(const QString& exePath, HWND hwnd) {
    auto group = WindowGroup{};
    group.exePath = exePath;
    group.icon = Util::getCachedIcon(exePath, hwnd);   // 有缓存，不慢
    group.addWindow({Util::getWindowTitle(hwnd), Util::getClassName(hwnd), hwnd});

    auto* item = new QListWidgetItem(group.icon, {});
    item->setData(Qt::UserRole, QVariant::fromValue(group));
    item->setSizeHint(lw->gridSize());

    const bool intoSecond = cfg.getNewAppSortSecond() && lw->count() >= 1;
    const int row = intoSecond ? 1 : lw->count();
    lw->insertItem(row, item);
    return row;
}

void Widget::removeEmptyGroups() {
    for (int i = lw->count() - 1; i >= 0; --i) {
        auto group = lw->item(i)->data(Qt::UserRole).value<WindowGroup>();
        for (int j = group.windows.size() - 1; j >= 0; --j)
            if (!IsWindow(group.windows[j].hwnd))
                group.windows.removeAt(j);
        if (group.windows.isEmpty()) {
            const bool wasCurrent = (lw->currentRow() == i);
            delete lw->takeItem(i);
            if (wasCurrent) lw->setCurrentRow(qBound(0, i, qMax(0, lw->count() - 1)));
        } else {
            lw->item(i)->setData(Qt::UserRole, QVariant::fromValue(group));
        }
    }
}

void Widget::onWindowsGone(const QList<HWND>&) {
    if (mode_ != Mode::AppMode || !this->isVisible()) return;
    removeEmptyGroups();
}
```

- [ ] **Step 5: 手动导航打标记**

在 `keyPressEvent` 里，凡是**用户手动移动选中**的分支都加 `userNavigated_ = true;`：Tab / Shift+Tab、左右键、上下键、Vim 映射。以及在 `eventFilter` 的滚轮分支里也加（`widget.cpp:427` 附近 `lw->setCurrentItem(item)` 之后）。

- [ ] **Step 6: 删掉 Task 2 的临时脚手架**

把 `repo/src/main.cpp` 里 `//! 临时脚手架` 那段和它上面的注释整段删除。若因此 `#include "utils/WindowWatcher.h"` 变得未被使用，一并删掉该 include。

- [ ] **Step 7: 构建**

Run: `rebuild.bat`
Expected: 构建成功，且**不再有任何 `[watch]` 调试输出**。

- [ ] **Step 6b: 把被轮询放大的既有日志降噪，并给 `start()` 加使用约束**

**降噪**（这是**本次改动造成**的热点，所以顺手处理）：轮询把两条既有日志从"每次用户动作一次"变成"每秒约 3 次"。

- `src/utils/Util.cpp:325` `qDebug() << "#List Valid Windows";` —— 无条件打印，每个 tick 都会走到。**删掉这一行**。它除了"这个函数被调了"不携带任何信息，而它的 5 个调用点各自都有更有价值的日志。删之前先 `grep -rn "#List Valid Windows"` 确认没有别处依赖它（这条 grep 要有阳性对照：改完再跑一次应当为空，而删之前应当非空）。
- `src/utils/Util.cpp:339` `qDebug() << "#ignore elevated:" << hwnd << getWindowTitle(hwnd);` —— 每个被跳过的提权窗口打印一次，而且**额外做一次跨进程的 `getWindowTitle()`**（`Util.cpp:339`），非管理员运行时（代码注释自己说这是常见情况）会放大到 3.3Hz × N。**这一条不在本 Task 里改**，只在报告里写明你观察到的放大倍数，留给最终复审分诊——因为它和上面那条同在一个被 5 个调用点共用的枚举函数里，改它要单独权衡。

**`start()` 的使用约束**（`WindowWatcher.cpp:26-28`）：`start()` 里的 `tracker_.seed()` 是**无条件**执行的，只有 `timer_.start()` 被 `!isActive()` 保护。所以在已运行时重复调用 `start()`，会把"上一 tick 之后新建的窗口"一并吃进 seed，那些窗口**永远不会被报出来**（静默丢通知）。本 Task 新增的每一处 `start()` 调用都必须满足"只在从停止状态恢复时调用"；宁可写成 `stop(); start();`，也不要对可能正在运行的 watcher 裸调 `start()`。

- [ ] **Step 8: 交用户跑 —— AppMode 动态扫描的四种开关组合**

**不要自己声称已验证。** 全部默认关闭，逐条改 `config.ini` 后重启 AltTaber：

- [ ] **A 关、B 关（默认）**：按 Alt+Tab 唤出切换器，另开一个新程序 —— 预期列表**不变化**，选中**不跳**
- [ ] **A 开、B 关**：同样操作 —— 预期新程序**出现在列表里**，但选中**不动**
- [ ] **A 开、B 开**：同样操作 —— 预期新程序出现**且选中跳到它**；松手切过去
- [ ] **A 开、B 开 + 手动优先**：唤出切换器 → 先按两下 Tab → 再开一个新程序 —— 预期新程序入列，但选中**不跳**（你手动翻过）
- [ ] **对称移除**：开着一个程序 → 唤出切换器 → 关掉它 —— 预期它**从列表消失**；若它正是当前选中，选中应顺延到相邻项而不是越界
- [ ] **既有功能未坏**：黑名单里的程序仍被过滤；`new_app_second` 行为不变；Alt+` 组列表仍正常
- [ ] **性能**：唤出切换器后静置 10 秒 —— 预期列表不卡顿、CPU 无明显尖峰（每 tick 约 2ms）

- [ ] **Step 9: 提交**

```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git add header/widget.h src/widget.cpp src/main.cpp
git commit -m "feat(watch): live-insert and auto-select new windows in the app list"
```

---

### Task 10: GroupMode 动态扫描

**Files:**
- Modify: `repo/header/widget.h`（加 `currentGroupExePath_`）
- Modify: `repo/src/widget.cpp`（`enterGroupMode` 启动轮询；`onNewWindows` / `onWindowsGone` 加模式分支）

**Interfaces:**
- Consumes: Task 7/8/9 的全部
- Produces: `onNewWindows` / `onWindowsGone` 在 `Mode::GroupMode` 下的行为

- [ ] **Step 1: 在 `enterGroupMode()` 记录当前组，并启动轮询**

`repo/header/widget.h` 加成员 `QString currentGroupExePath_;`。

在 `enterGroupMode()` 里 `mode_ = Mode::GroupMode;` **之后**加：
```cpp
    currentGroupExePath_ = exePath;
    // 组列表同样要轮询新窗口，否则本 Task 的全部逻辑永远不会被触发
    if (cfg.getWatchNewWindows()) watcher_->start();
    else watcher_->stop();
```
**这一步不能漏**：`requestShow()` 里的启动只覆盖 AppMode；`enterGroupMode()` 不启动的话，GroupMode 的动态扫描会静默地永不生效——功能看起来"写完了"，实际一次都不会跑。

- [ ] **Step 2: 给两个处理器加 GroupMode 分支**

**顺序陷阱（2026-09-30 由 Task 7 的实现者指出，已核实）**：本步原来的写法是"在 `onNewWindows()` 的 `if (mode_ != Mode::AppMode || ...) return;` **之后**插入 GroupMode 分支"——**那是死代码**，守卫会先把 GroupMode 挡回去，新分支一次都跑不到。

正确顺序是**先放宽守卫、再插分支，且分支在 AppMode 逻辑之前**。Task 9 留下的那行是：
```cpp
    if (mode_ != Mode::AppMode || !this->isVisible()) return;
```
把它**替换**为：
```cpp
    if (!this->isVisible()) return;                 // 放宽：GroupMode 也要放进来
    if (mode_ == Mode::GroupMode) { /* GroupMode 分支，必须在这里 */ }
```
然后才接原有的 AppMode 逻辑。**判据**：改完之后，`grep -A5 "onNewWindows"` 里 GroupMode 分支的位置必须在任何 `mode_ != Mode::AppMode` 判定之前。
```cpp
    if (mode_ == Mode::GroupMode) {
        // 只关心本进程的新窗口
        int lastInserted = -1;
        for (auto hwnd: added) {
            if (Util::getWindowProcessPath(hwnd).compare(currentGroupExePath_, Qt::CaseInsensitive) != 0)
                continue;
            // 插到列表末尾：新窗口没有使用记录，按最后使用时间降序它排最后
            WindowInfo info{Util::getWindowTitle(hwnd), Util::getClassName(hwnd), hwnd};
            lastInserted = groupList_->append(info);
        }
        if (lastInserted < 0) return;
        if (cfg.getAutoSelectNew() && !userNavigated_)
            groupList_->setCurrentRow(lastInserted);
        // 行数变了，尺寸要重算
        layoutForCurrentMode();
        return;
    }
```
并在 `onWindowsGone()` 里、`if (mode_ != Mode::AppMode ...)` 之前加：
```cpp
    if (mode_ == Mode::GroupMode && this->isVisible()) {
        bool changed = false;
        for (auto hwnd: removed) {
            const bool wasCurrent = (groupList_->currentHwnd() == hwnd);
            const int row = groupList_->removeByHwnd(hwnd);
            if (row < 0) continue;
            changed = true;
            if (wasCurrent) groupList_->setCurrentRow(row);   // 顺延到同下标（钳位在 setCurrentRow 内）
        }
        if (changed) layoutForCurrentMode();
        return;
    }
```
注意 `onWindowsGone` 现在的第一行是 `if (mode_ != Mode::AppMode || !this->isVisible()) return;` —— 这个新分支必须放在它**之前**，否则 GroupMode 永远进不来。

- [ ] **Step 3: 构建**

Run: `rebuild.bat`
Expected: 构建成功。

- [ ] **Step 4: 交用户跑 —— GroupMode 动态扫描**

**不要自己声称已验证。** 用浏览器这类容易开新窗口的程序，A/B 都开：

- [ ] 在一个多窗口程序里按 Alt+` 弹出组列表，**不松手** → 用快捷键（如浏览器 Ctrl+N）开一个新窗口 —— 预期新窗口**出现在列表里**且**选中跳到它**，松手切过去
- [ ] 同上，但弹出后**先按一下 `** 手动翻过 → 再开新窗口 —— 预期新窗口入列但**选中不跳**
- [ ] 弹出组列表后关掉其中一个窗口 —— 预期它**从列表消失**；若它正是选中项，选中顺延而**不越界**
- [ ] 弹出组列表后开一个**别的程序**的新窗口 —— 预期组列表**不受影响**（只收本进程的）
- [ ] 行数增加超过 8 行 —— 预期列表高度封顶、内部可滚动，不撑破屏幕

- [ ] **Step 4b: 核对配置文档与落地实现是否一致**

`CONFIG.md` / `CONFIG_zh.md` 在 Task 4 里就写下了三条承诺，而它们的实现**直到本 Task 才存在**：300ms 轮询、窗口关闭时对称移除、"让位于手动操作"。现在实现落地了，**逐条回读这两份文档，与代码实际行为对照**：

- 轮询间隔是不是 300ms？（看 `WindowWatcher::IntervalMs`）不一致就改文档**或**改常量，二选一，别放着
- 关闭的窗口是否真的被移除？被移除的正是选中项时，选中怎么走？
- "手动优先"写的是"Tab / ` / 方向键 / 滚轮"，与 `userNavigated_` 的实际置位点是否一一对应？

**判据**：文档里每一条关于 `[dynamic]` 的行为描述，都要能在代码里指出对应位置（文件:行号）。指不出来的就改掉文档。把对照结果写进报告。

- [ ] **Step 5: 提交**

```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git add header/widget.h src/widget.cpp
git commit -m "feat(watch): live-insert new windows into the Alt+grave group list"
```

---

### Task 11: 收尾 —— 交互验证清单、交接文档、完整回归

**Files:**
- Create: `2026-09-30-group-window-list-and-dynamic-scan-checklist.md`
- Modify: `2026-06-16_alttaber_research/HANDOFF.md`

- [ ] **Step 1: 写交互验证清单**

把上面各 Task 里所有「交用户跑」的条目汇总成一份**可打勾**的清单文件，写在设计文档同级的 `docs/` 下。每条要写清：操作、预期、以及**失败时该回头查哪个 Task 的哪一步**（把失效模式直接连到排查入口）。

- [ ] **Step 2: 更新 HANDOFF.md**

在 `2026-06-16_alttaber_research/HANDOFF.md` 末尾追加本轮交接：新分支名、改了什么文件、两个新配置键、设计/计划/清单三份文档的路径、以及**目前未经用户验证的部分**（明确列出，不含糊）。

- [ ] **Step 3: 完整回归**

Run: `rebuild.bat`，然后手动跑一行 windeployqt（**不要跑 `deploy.bat`，它有 `pause`**）：
```bash
"D:/a_my/tool/Qt/6.8.1/msvc2022_64/bin/windeployqt.exe" "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo/build/Release/AltTaber.exe"
```
然后把清单**整体**交给用户跑一遍，拿到逐条结果。**不要自己声称已验证。**

- [ ] **Step 4: 提交并合并回 fork**

```bash
cd "D:/a_my/project/workspaces_AND_tool-agnostic-projects/Daily_Claude/project/2026-06-16_alttaber_research/repo"
git add ../HANDOFF.md
git commit -m "docs: add interactive verification checklist and handoff notes"
git push fork HEAD
```
推之前先跟用户确认分支名，别自作主张 push。

---

## §Task 7 计划勘误（执行后补记，2026-09-30）

实现者报回两处**计划自身的缺口**，均已用 `grep` 核实（计划全文：`forceShow` **0 命中**；`VimArrows|Key_Up|Key_Tab` **0 命中**）。两处都不是实现缺陷。

### (a) 漏了显示路径 —— 会导致列表永远不出现

Step 4b 抽出的 `layoutForCurrentMode()` 只做几何与两个列表的显隐切换，**不含 `forceShow()`**；而**入口 A 触发时 `Widget` 是隐藏的**，于是"设好几何 + `show()` 子控件"在不可见的父窗口上等于什么都没发生。Step 5 的 `requestShowGroup()` 也没补这一步。

正确做法（实现者已落地，`src/widget.cpp:466-470`）：

```cpp
    // 沿用 AppMode 那套防闪烁的 forceShow()（见设计文档 §2.4）
    if (mode_ == Mode::GroupMode && !this->isVisible())
        forceShow();
```

`!isVisible()` 这个守卫**不能省**：入口 B 是"已可见时就地换内容"，此时再 `forceShow()` 会走 `showMinimized()/showNormal()`，凭空造成一次闪烁。

### (b) 漏了 GroupMode 下的按键路由 —— 会意外最小化窗口

设计文档 §2.3 早已定下语义（`Tab`/`Shift+Tab` ≡ `` ` ``/`Shift+`` ` ``；`↑↓←→` 与 Vim `HJKL` 映射为上下移），但计划里**没有任何一步**把它翻成实现。后果不是"少个功能"，而是**做错事**：

- GroupMode 下按方向键会落进 AppMode 分支，把滚轮事件投给**已隐藏的 `lw`** → 触发 `eventFilter` 里的组内轮换逻辑 → **可能意外最小化窗口**
- 按 `Tab` 会静默改动隐藏列表的选中项

实现者**没有自行发明语义**（这是对的），按流程报了上来。补法（由 Task 7 的修复轮实现）：

- `keyPressEvent` 里每个"移动选中"的分支都按 `mode_` 分流：GroupMode → `groupList_->moveSelection(delta)`，并且每次手动移动都要置 `userNavigated_ = true`
- 对应关系：`Tab`/`Shift+Tab` → `+1`/`-1`；`↓`/`J`、`↑`/`K` → `+1`/`-1`；`→`/`L`、`←`/`H` → `+1`/`-1`
- GroupMode 下**不得**把方向键转发成发给 `lw` 的滚轮事件

---

## §偏差记录（本计划 vs 设计文档）

写本计划时发现设计文档 §4 有一处**事实错误**，已按下列方式处理，需用户确认：

| 设计文档 §4 的说法 | 实际情况 | 本计划的做法 |
|---|---|---|
| 把 `rotateWindowInGroup` / `buildGroupWindowOrder` / `sortGroupWindows` / `getLastActiveGroupWindow` 挪进 `GroupWindowList`，"零额外风险" | **判断错误**。后三个都依赖 `Widget::winActiveOrder` 这个由 `notifyForegroundChanged` 写入的成员状态，而它同时被**任务栏滚轮链路**（`rotateTaskbarWindowInGroup`）读写。搬它们就要把状态一起搬走，会波及一条独立链路 | **用户 2026-09-30 决定：搬**，但改成本计划的 **Task 5** 单独做——把 `winActiveOrder` 连同那 4 个函数抽成 **`WindowActivationOrder`**（而不是塞进 `GroupWindowList`，那样会把排序职责塞给一个展示类），并且**只搬家不改行为**，搬完立刻单独验证任务栏滚轮 / Alt+Tab / Alt+` 三条链路的旧行为未变。`GroupWindowList`（Task 6）仍只做展示与选中 |
| `ui/widget.ui` 加竖排 `QListWidget` | 手改 .ui XML 不如代码里建来得稳，且本仓所有列表样式本来就是在代码里配的（`widget.cpp:33-49`） | 由 `GroupWindowList` 在构造函数里 `new QListWidget`，不动 `widget.ui` |
| 组件清单里没有 `WindowDiffTracker` / `WindowInfo.h` | 为了让 diff 逻辑**不依赖 Qt**（能被 Task 1 单测覆盖），它是必需的 | 新增这两个文件，并说明理由 |

设计文档里的其他决定（两条入口与各自初始高亮、仅标题+右省略、宽度 clamp 220–480、8 行封顶、300ms 间隔、手动优先、对称移除、沿用现有排序语义、复用同一 HWND）本计划**逐条照做**。
