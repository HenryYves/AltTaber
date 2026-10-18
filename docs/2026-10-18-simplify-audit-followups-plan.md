# Simplify Audit Follow-ups Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the dormant API surface, make the group-list/thumbnail layout a single source of truth, and drop the redundant `build.bat` script.

**Architecture:** Three independent cuts: (1) delete unused public members from utility wrappers, (2) delete the unused UIAutomation helper cluster, (3) extract a pure `WindowGeometry::groupContent()` helper and a `thumbnailActive_` state so `layoutForCurrentMode()` and `updateThumbnail()` stop duplicating group geometry. Then consolidate `build.bat` into `rebuild.bat`.

**Tech Stack:** C++20, Qt 6.8.1 Widgets, MSVC / Visual Studio 2026, CMake, Windows 11.

## Global Constraints

- Work in `D:\a_my\project\Forks\AltTaber\repo`, branch `master` at `afeccd4` or later. Do not touch `feat/group-window-list-and-live-scan` or `overnight/2026-10-17-multimonitor-thumbnail`.
- Build command: `cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat" > "$TEMP/alttaber_build.log" 2>&1`. Must exit 0 and `grep -c "warning C" "$TEMP/alttaber_build.log"` must be `0`.
- Close `AltTaber.exe` before rebuilding: it locks `repo\build\Release\AltTaber.exe`.
- Tests: `repo\build\tests\Release\WindowGeometryTests.exe` and `WindowDiffTests.exe`. Baseline before this plan: 38/38 and 18/18. After Task 3 the geometry count increases; both must exit 0.
- Keep `header/utils/Startup.h`'s `#if __has_include("ScheduledTask.h")` fallback. User decision 2026-10-18: the fallback build mode stays.
- Do not modify historical documents under `docs/` (`2026-09-30-*`, `2026-10-07-*`, `2026-10-17-*`) or `.superpowers/`.
- No emoji in code, comments, docs, or commit messages.
- Files outside `repo/` (`rebuild.bat`, `build.bat`, `HANDOFF.md`) are not git-tracked. Their steps have no commit; only repo files are committed.
- Commit only files changed by the task; never `git add -A`.

---

## File Structure

| File | Responsibility |
|---|---|
| `header/utils/setWindowBlur.h`, `src/utils/setWindowBlur.cpp` | Keep `setWindowAccent()`; remove the orphaned `setWindowBlur()` wrapper |
| `header/utils/AppUtil.h`, `src/utils/AppUtil.cpp` | Remove unused `getAppFrameWindow()`; keep the AppFrame class constant and `isAppFrameWindow()` |
| `header/utils/Startup.h` | Remove unused `set()`; keep the scheduled-task conditional |
| `header/utils/WindowWatcher.h` | Remove unused `isRunning()` |
| `header/utils/WindowActivationOrder.h`, `src/utils/WindowActivationOrder.cpp` | Remove unused `clear()` |
| `header/utils/ConfigManagerBase.h` | Remove unused `remove()` |
| `header/utils/GroupWindowList.h`, `src/utils/GroupWindowList.cpp` | Remove unused `count()` / `currentRow()` wrappers |
| `header/utils/DwmThumbnail.h` | Remove unused `source()` accessor |
| `header/utils/SystemTray.h` | Replace the tray admin emoji with `(admin)` |
| `header/utils/uiautomation.h`, `src/utils/uiautomation.cpp` | Remove the unused UIAutomation/UIElement helper cluster |
| `header/utils/WindowGeometry.h` | Add pure `GroupContent` + `groupContent()` |
| `tests/window_geometry_test.cpp` | Add group-content assertions |
| `header/widget.h` | Add `thumbnailActive_` |
| `src/widget.cpp` | Route group layout and thumbnail update through the helper |
| `D:\a_my\project\Forks\AltTaber\rebuild.bat` | Create `build/` when missing |
| `D:\a_my\project\Forks\AltTaber\build.bat` | Delete |

---

### Task 1: Remove dormant APIs and small cleanups

**Files:**
- Modify: `header/utils/setWindowBlur.h`, `src/utils/setWindowBlur.cpp`
- Modify: `header/utils/AppUtil.h`, `src/utils/AppUtil.cpp`
- Modify: `header/utils/Startup.h`
- Modify: `header/utils/WindowWatcher.h`
- Modify: `header/utils/WindowActivationOrder.h`, `src/utils/WindowActivationOrder.cpp`
- Modify: `header/utils/ConfigManagerBase.h`
- Modify: `header/utils/GroupWindowList.h`, `src/utils/GroupWindowList.cpp`
- Modify: `header/utils/DwmThumbnail.h`, `src/widget.cpp`
- Modify: `header/utils/SystemTray.h`

**Interfaces:**
- Consumes: nothing.
- Produces: `setWindowAccent(HWND, bool)` stays available; every removed symbol has no callers.

- [ ] **Step 1: Capture the baseline**

Run:

```bash
taskkill //IM AltTaber.exe //F
cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat" > "$TEMP/alttaber_build.log" 2>&1
echo "build_exit=$?"
grep -c "warning C" "$TEMP/alttaber_build.log"
"D:\a_my\project\Forks\AltTaber\repo\build\tests\Release\WindowGeometryTests.exe" | tail -1
"D:\a_my\project\Forks\AltTaber\repo\build\tests\Release\WindowDiffTests.exe" | tail -1
```

Expected: `build_exit=0`, warning count `0`, `38/38 passed`, `18/18 passed`.

- [ ] **Step 2: Delete `setWindowBlur()`**

In `header/utils/setWindowBlur.h`, delete:

```cpp
void setWindowBlur(HWND hWnd);
```

In `src/utils/setWindowBlur.cpp`, delete:

```cpp
void setWindowBlur(HWND hWnd) {
    setWindowAccent(hWnd, true);
}
```

Keep `setWindowAccent()` and the include of `utils/setWindowBlur.h` in `widget.cpp`.

- [ ] **Step 3: Delete `AppUtil::getAppFrameWindow()`**

In `header/utils/AppUtil.h`, delete:

```cpp
HWND getAppFrameWindow(HWND hwnd);
```

In `src/utils/AppUtil.cpp`, delete the whole `getAppFrameWindow` function (the block beginning `HWND getAppFrameWindow(HWND hwnd) {` and ending with its closing brace before `getAppCoreWindow`). Keep `AppFrameWindowClass` and `isAppFrameWindow()`.

- [ ] **Step 4: Delete `Startup::set()`**

In `header/utils/Startup.h`, delete:

```cpp
    static void set(bool _on) {
        _on ? on() : off();
    }
```

Do not remove the `#if __has_include("ScheduledTask.h")` block.

- [ ] **Step 5: Delete `WindowWatcher::isRunning()`**

In `header/utils/WindowWatcher.h`, delete:

```cpp
    bool isRunning() const { return timer_.isActive(); }
```

- [ ] **Step 6: Delete `WindowActivationOrder::clear()`**

In `header/utils/WindowActivationOrder.h`, delete:

```cpp
    void clear();
```

In `src/utils/WindowActivationOrder.cpp`, delete:

```cpp
void WindowActivationOrder::clear() {
    order_.clear();
}
```

- [ ] **Step 7: Delete `ConfigManagerBase::remove()`**

In `header/utils/ConfigManagerBase.h`, delete:

```cpp
    void remove(QAnyStringView key) {
        settings.remove(key);
    }
```

- [ ] **Step 8: Delete `GroupWindowList::count()` and `currentRow()`**

In `header/utils/GroupWindowList.h`, delete:

```cpp
    int count() const;
    int currentRow() const;
```

In `src/utils/GroupWindowList.cpp`, delete:

```cpp
int GroupWindowList::count() const {
    return list_->count();
}

int GroupWindowList::currentRow() const {
    return list_->currentRow();
}
```

Do not change internal uses of `list_->count()` / `list_->currentRow()`.

- [ ] **Step 9: Delete `DwmThumbnail::source()` and the redundant outer check**

In `header/utils/DwmThumbnail.h`, delete:

```cpp
    [[nodiscard]] HWND source() const { return source_; }
```

In `src/widget.cpp`, replace:

```cpp
    // 源 hwnd 未变则跳过注册（setSource() 自己也会短路，这里省掉一次调用）
    if (thumbnail_->source() != source) {
        if (!thumbnail_->setSource(source)) {
            hideThumbnail();   // 注册失败只静默降级为不显示，不影响窗口切换
            return;
        }
    }
```

with:

```cpp
    // setSource() 自己会对相同源短路；外层不再重复判断
    if (!thumbnail_->setSource(source)) {
        hideThumbnail();   // 注册失败只静默降级为不显示，不影响窗口切换
        return;
    }
```

- [ ] **Step 10: Make `sortByActiveOrder()` private**

In `header/utils/WindowActivationOrder.h`, move this declaration from the public section to the private section:

```cpp
    /// 按活跃时间倒序重排 windows；该进程没有任何记录时**保持原顺序不动**
    void sortByActiveOrder(QList<HWND>& windows, const QString& exePath) const;
```

No `.cpp` change: both callers are inside `WindowActivationOrder`.

- [ ] **Step 11: Remove the tray emoji**

In `header/utils/SystemTray.h`, replace the `act_startup->setText(...)` line that appends the key emoji (U+1F511) with:

```cpp
                act_startup->setText(text + " (admin)");
```

- [ ] **Step 12: Build and run the existing tests**

Run:

```bash
cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat" > "$TEMP/alttaber_build.log" 2>&1
echo "build_exit=$?"
grep -n "error C\|error LNK" "$TEMP/alttaber_build.log"
grep -c "warning C" "$TEMP/alttaber_build.log"
"D:\a_my\project\Forks\AltTaber\repo\build\tests\Release\WindowGeometryTests.exe" | tail -1
"D:\a_my\project\Forks\AltTaber\repo\build\tests\Release\WindowDiffTests.exe" | tail -1
```

Expected: `build_exit=0`, no `error C`/`error LNK` lines, warning count `0`, `38/38 passed`, `18/18 passed`.

- [ ] **Step 13: Prove no references remain**

Run:

```bash
grep -rn "setWindowBlur\|getAppFrameWindow\|Startup::set\|WindowActivationOrder::clear\|ConfigManagerBase::remove\|groupList_->count\|groupList_->currentRow\|isRunning() const\|thumbnail_->source\|DwmThumbnail::source" src header tests
```

Expected: no output. (`SingleApp::isRunning()` has a different signature and is not matched.)

Then confirm the tray label and emoji:

```bash
grep -n 'text + " (admin)"' header/utils/SystemTray.h
grep -nP '\xF0\x9F\x94\x91' header/utils/SystemTray.h
```

Expected: first command prints the `(admin)` line; second prints nothing.

- [ ] **Step 14: Commit**

```bash
cd "D:/a_my/project/Forks/AltTaber/repo"
git add header/utils/setWindowBlur.h src/utils/setWindowBlur.cpp \
        header/utils/AppUtil.h src/utils/AppUtil.cpp \
        header/utils/Startup.h header/utils/WindowWatcher.h \
        header/utils/WindowActivationOrder.h src/utils/WindowActivationOrder.cpp \
        header/utils/ConfigManagerBase.h \
        header/utils/GroupWindowList.h src/utils/GroupWindowList.cpp \
        header/utils/DwmThumbnail.h src/widget.cpp header/utils/SystemTray.h
git commit -m "refactor: remove dormant APIs and tray emoji"
```

---

### Task 2: Remove unused UIAutomation helpers

**Files:**
- Modify: `header/utils/uiautomation.h`, `src/utils/uiautomation.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `UIAutomation::getElementUnderMouse()`, `UIAutomation::cleanup()`, `UIElement::isValid()`, `UIElement::getName()`, `UIElement::getClassName()`, `UIElement::getAutomationId()` remain.

- [ ] **Step 1: Delete the declarations**

In `header/utils/uiautomation.h`, delete these lines:

```cpp
    static UIElement getParentWithHWND(const UIElement& element);
```

```cpp
    IUIAutomationElement* inner() const { return pElement; }
```

```cpp
    QRect getBoundingRect() const;
    CONTROLTYPEID getControlType() const; // e.g.`UIA_PaneControlTypeId`
    HWND getNativeWindowHandle() const;
    QString getNativeWindowClass() const;
    // 如果自身的HWND为空，则循环查找HWND不为空的父元素的ClassName
    QString getSelfOrParentNativeWindowClass() const;
```

Keep `isValid()`, `getName()`, `getClassName()`, `getAutomationId()`.

- [ ] **Step 2: Delete the definitions**

In `src/utils/uiautomation.cpp`, delete the whole functions:

- `UIAutomation::getParentWithHWND`
- `UIElement::getBoundingRect`
- `UIElement::getControlType`
- `UIElement::getNativeWindowHandle`
- `UIElement::getNativeWindowClass`
- `UIElement::getSelfOrParentNativeWindowClass`

Do not touch `UIAutomation::getElementUnderMouse`, `UIAutomation::cleanup`, `UIElement::getName`, `UIElement::getClassName`, `UIElement::getAutomationId`, or `UIAutomation::init`.

- [ ] **Step 3: Remove now-unused includes only if the compiler flags them**

If `src/utils/uiautomation.cpp` or `header/utils/uiautomation.h` no longer uses `QRect` or `QDebug`, remove those includes. Otherwise leave them.

- [ ] **Step 4: Build and test**

```bash
cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat" > "$TEMP/alttaber_build.log" 2>&1
echo "build_exit=$?"
grep -n "error C\|error LNK" "$TEMP/alttaber_build.log"
grep -c "warning C" "$TEMP/alttaber_build.log"
"D:\a_my\project\Forks\AltTaber\repo\build\tests\Release\WindowGeometryTests.exe" | tail -1
"D:\a_my\project\Forks\AltTaber\repo\build\tests\Release\WindowDiffTests.exe" | tail -1
```

Expected: `build_exit=0`, no errors, warning count `0`, `38/38 passed`, `18/18 passed`.

- [ ] **Step 5: Prove no references remain**

```bash
grep -rn "getParentWithHWND\|getBoundingRect\|getControlType\|getNativeWindowHandle\|getNativeWindowClass\|getSelfOrParentNativeWindowClass\|inner()" src header tests
```

Expected: no output.

- [ ] **Step 6: Commit**

```bash
cd "D:/a_my/project/Forks/AltTaber/repo"
git add header/utils/uiautomation.h src/utils/uiautomation.cpp
git commit -m "refactor: remove unused UIAutomation helpers"
```

---

### Task 3: Unify group-list and thumbnail layout

**Files:**
- Modify: `header/utils/WindowGeometry.h`
- Modify: `tests/window_geometry_test.cpp`
- Modify: `header/widget.h`
- Modify: `src/widget.cpp`

**Interfaces:**
- Consumes: existing `WindowGeometry::thumbnailRect`, `GroupWindowList::MinWidth`, `Widget::thumbnailBoxSize()`.
- Produces: `WindowGeometry::GroupContent` and `WindowGeometry::groupContent(...)`; `Widget::thumbnailActive_`.

- [ ] **Step 1: Write the failing tests**

In `tests/window_geometry_test.cpp`, add this block before the final `return g_failed == 0 ? 0 : 1;`:

```cpp
    // group content layout: one source of truth for the list and the thumbnail box
    {
        auto c = WindowGeometry::groupContent(300, 120, 320, 180, 12, 220, 1600, true);
        check(c.thumbnailActive, "groupContent: enough room activates the thumbnail");
        check(c.contentWidth == 300 + 12 + 320, "groupContent: content width includes gap and box");
        check(c.contentHeight == 180, "groupContent: content height is the taller side");
        check(c.listY == (180 - 120) / 2, "groupContent: list is vertically centered");
    }
    {
        auto c = WindowGeometry::groupContent(300, 120, 320, 180, 12, 220, 300, true);
        check(!c.thumbnailActive, "groupContent: narrow content hides the thumbnail");
        check(c.contentWidth == 300, "groupContent: hidden thumbnail does not widen content");
        check(c.contentHeight == 120, "groupContent: hidden thumbnail does not raise content");
        check(c.listY == 0, "groupContent: list starts at zero when thumbnail is off");
    }
    {
        auto c = WindowGeometry::groupContent(300, 400, 320, 180, 12, 220, 1600, false);
        check(!c.thumbnailActive, "groupContent: disabled thumbnail stays inactive");
        check(c.contentHeight == 400, "groupContent: list height wins when thumbnail is off");
    }
    {
        auto c = WindowGeometry::groupContent(0, 0, 0, 0, 12, 220, 0, true);
        check(!c.thumbnailActive, "groupContent: zero box cannot become active");
        check(c.contentWidth == 0 && c.contentHeight == 0, "groupContent: zero inputs stay zero");
    }
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat" > "$TEMP/alttaber_build.log" 2>&1
echo "build_exit=$?"
grep -n "groupContent" "$TEMP/alttaber_build.log" | head
```

Expected: `build_exit` nonzero and a compile error naming `groupContent` (the function does not exist yet).

- [ ] **Step 3: Add the pure helper**

In `header/utils/WindowGeometry.h`, add after `fitAspect()` and before `thumbnailRect()`:

```cpp
/// GroupMode 内容区的唯一几何来源：缩略图是否放得下、内容尺寸、列表在内容区里的偏移。
/// 输入都是逻辑像素；`availableContentWidth` 已扣掉弹窗左右 margin。
struct GroupContent {
    int contentWidth = 0;
    int contentHeight = 0;
    int listX = 0;
    int listY = 0;
    bool thumbnailActive = false;
};

inline GroupContent groupContent(int listWidth, int listHeight,
                                 int thumbBoxWidth, int thumbBoxHeight,
                                 int gap, int minListWidth,
                                 int availableContentWidth, bool thumbnailRequested) {
    const int lw = std::max(0, listWidth);
    const int lh = std::max(0, listHeight);
    const int bw = std::max(0, thumbBoxWidth);
    const int bh = std::max(0, thumbBoxHeight);
    const int g = std::max(0, gap);
    const int avail = std::max(0, availableContentWidth);
    const bool active = thumbnailRequested && (std::max(0, minListWidth) + g + bw <= avail);

    GroupContent out;
    out.thumbnailActive = active;
    out.contentWidth = active ? lw + g + bw : lw;
    out.contentHeight = active ? std::max(lh, bh) : lh;
    out.listX = 0;
    out.listY = (out.contentHeight - lh) / 2;
    return out;
}
```

- [ ] **Step 4: Run the geometry tests**

```bash
cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat" > "$TEMP/alttaber_build.log" 2>&1
echo "build_exit=$?"
grep -c "warning C" "$TEMP/alttaber_build.log"
"D:\a_my\project\Forks\AltTaber\repo\build\tests\Release\WindowGeometryTests.exe" | tail -1
```

Expected: `build_exit=0`, warning count `0`, a line like `50/50 passed` (38 baseline + 12 new assertions).

- [ ] **Step 5: Add `thumbnailActive_`**

In `header/widget.h`, next to `backgroundSnapshot_`/`thumbnail_` members, add:

```cpp
    /// 本次 GroupMode 弹窗是否真的启用了缩略图（布局按可用宽度算出来的结果）。
    /// scheduleThumbnailUpdate()/updateThumbnail() 必须读它，不能只读配置，
    /// 否则窄屏上布局先隐藏、换高亮又把缩略图放回来。
    bool thumbnailActive_ = false;
```

- [ ] **Step 6: Route `layoutForCurrentMode()` through the helper**

At the top of `layoutForCurrentMode()`, next to `QSize contentSize;`, add:

```cpp
    QPoint groupListOffset{0, 0};
```

In `src/widget.cpp`, replace the GroupMode geometry block (currently the `bool thumbOn = ...` / `thumbBox` / `setWidthCap` / `contentSize = thumbOn ? ...` section) with:

```cpp
        bool thumbRequested = cfg.getGroupThumbnailEnabled();
        const QSize thumbBox = thumbRequested ? thumbnailBoxSize() : QSize();
        int availContentW = 0;
        if (auto* screen = targetScreen()) {
            availContentW = screen->availableGeometry().width()
                            - ListWidgetMargin.left() - ListWidgetMargin.right();
            int cap = availContentW;
            if (thumbRequested) cap -= thumbBox.width() + ThumbnailGap;
            groupList_->setWidthCap(qMax(GroupWindowList::MinWidth, cap));
        }
        const QSize listSize = groupList_->preferredSize();
        const auto group = WindowGeometry::groupContent(
                listSize.width(), listSize.height(),
                thumbBox.width(), thumbBox.height(),
                ThumbnailGap, GroupWindowList::MinWidth,
                availContentW, thumbRequested);
        thumbnailActive_ = group.thumbnailActive;
        groupListOffset = QPoint(group.listX, group.listY);
        view->setFixedSize(listSize);
        view->show();
        contentSize = QSize(group.contentWidth, group.contentHeight);
        if (thumbnailActive_) {
            if (thumbnailHost_) thumbnailHost_->hide();
            scheduleThumbnailUpdate();
        } else {
            hideThumbnail();
        }
```

In the AppMode branch, set `thumbnailActive_ = false;` immediately before `hideThumbnail();`.

In the GroupMode content move (currently `view->move(contentRect.topLeft() + QPoint(0, (contentRect.height() - view->height()) / 2));`), change it to:

```cpp
        view->move(contentRect.topLeft() + groupListOffset);
```

- [ ] **Step 7: Route `updateThumbnail()` and `scheduleThumbnailUpdate()` through the helper**

In `scheduleThumbnailUpdate()`, change the guard to:

```cpp
    if (mode_ != Mode::GroupMode || !cfg.getGroupThumbnailEnabled() || !thumbnailActive_) {
        hideThumbnail();
        return;
    }
```

In `updateThumbnail()`, change the guard to include `!thumbnailActive_`:

```cpp
    if (mode_ != Mode::GroupMode || !this->isVisible()
        || !cfg.getGroupThumbnailEnabled() || !thumbnailActive_) {
        hideThumbnail();
        return;
    }
```

Then replace the duplicated content-size block with:

```cpp
    const QSize listSize = groupList_->preferredSize();
    const QSize box = thumbnailBoxSize();
    int availContentW = 0;
    if (auto* screen = targetScreen()) {
        availContentW = screen->availableGeometry().width()
                        - ListWidgetMargin.left() - ListWidgetMargin.right();
    }
    const auto group = WindowGeometry::groupContent(
            listSize.width(), listSize.height(),
            box.width(), box.height(),
            ThumbnailGap, GroupWindowList::MinWidth,
            availContentW, true);
    if (!group.thumbnailActive) {
        hideThumbnail();
        return;
    }

    const WindowGeometry::Rect listRect{
        0, 0,
        listSize.width() + ThumbnailGap + (box.width() - fitted.width) / 2,
        group.contentHeight};
    const auto thumbRect = WindowGeometry::thumbnailRect(
        listRect, fitted.width, fitted.height, 0,
        group.contentWidth, group.contentHeight);

    const QPoint contentTopLeft((this->width() - group.contentWidth) / 2,
                                (this->height() - group.contentHeight) / 2);
```

Keep the rest of `updateThumbnail()` (physical coordinate conversion, host show, DWM registration) unchanged.

- [ ] **Step 8: Build and run both tests**

```bash
cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat" > "$TEMP/alttaber_build.log" 2>&1
echo "build_exit=$?"
grep -n "error C\|error LNK" "$TEMP/alttaber_build.log"
grep -c "warning C" "$TEMP/alttaber_build.log"
"D:\a_my\project\Forks\AltTaber\repo\build\tests\Release\WindowGeometryTests.exe" | tail -1
"D:\a_my\project\Forks\AltTaber\repo\build\tests\Release\WindowDiffTests.exe" | tail -1
```

Expected: `build_exit=0`, no errors, warning count `0`, geometry tests all pass, `18/18 passed`.

- [ ] **Step 9: GUI smoke**

Launch `repo\build\Release\AltTaber.exe`; open two Notepad windows; press Alt+`` ` `` from one; confirm:

- the group list appears with the thumbnail on the right;
- moving the highlight updates the thumbnail;
- pressing ` from the app list (entry B) still shows the thumbnail;
- no thumbnail overlap or background gap appears.

If the app must run while the installed copy is active, close the installed copy first; two instances share the `AltTaber-MrBeanCpp` mutex.

- [ ] **Step 10: Commit**

```bash
cd "D:/a_my/project/Forks/AltTaber/repo"
git add header/utils/WindowGeometry.h tests/window_geometry_test.cpp header/widget.h src/widget.cpp
git commit -m "refactor: unify group-list and thumbnail layout"
```

---

### Task 4: Consolidate the build scripts

**Files:**
- Modify: `D:\a_my\project\Forks\AltTaber\rebuild.bat`
- Delete: `D:\a_my\project\Forks\AltTaber\build.bat`

**Interfaces:**
- Consumes: nothing.
- Produces: one build entrypoint, `rebuild.bat`, which creates `build/` when missing.

- [ ] **Step 1: Make `rebuild.bat` create the build directory**

Replace the current `rebuild.bat` contents with:

```bat
@echo off
call "D:\a_my\tool\vs2026\VC\Auxiliary\Build\vcvars64.bat" > nul
cd /d "D:\a_my\project\Forks\AltTaber\repo"
if not exist build mkdir build
cd build
cmake .. -G "Visual Studio 18 2026" -A x64
if %ERRORLEVEL% EQU 0 (
    cmake --build . --config Release
)
```

- [ ] **Step 2: Delete `build.bat`**

```bash
rm -f "D:/a_my/project/Forks/AltTaber/build.bat"
```

- [ ] **Step 3: Verify the consolidated script**

```bash
taskkill //IM AltTaber.exe //F
cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat" > "$TEMP/alttaber_build.log" 2>&1
echo "build_exit=$?"
grep -c "warning C" "$TEMP/alttaber_build.log"
```

Expected: `build_exit=0`, warning count `0`.

- [ ] **Step 4: No git commit**

`rebuild.bat` and `build.bat` live outside `repo/` and are not git-tracked. Record the change in `HANDOFF.md` in Task 5; do not try to commit these files.

---

### Task 5: Final validation and handoff

**Files:**
- Modify: `D:\a_my\project\Forks\AltTaber\HANDOFF.md` (outside the repo, no commit)

**Interfaces:**
- Consumes: all prior tasks.
- Produces: a recorded simplification summary and final verification evidence.

- [ ] **Step 1: Run the final full verification**

```bash
taskkill //IM AltTaber.exe //F
cmd //c "D:\a_my\project\Forks\AltTaber\rebuild.bat" > "$TEMP/alttaber_build.log" 2>&1
echo "build_exit=$?"
grep -n "error C\|error LNK" "$TEMP/alttaber_build.log"
grep -c "warning C" "$TEMP/alttaber_build.log"
"D:\a_my\project\Forks\AltTaber\repo\build\tests\Release\WindowGeometryTests.exe" | tail -1
"D:\a_my\project\Forks\AltTaber\repo\build\tests\Release\WindowDiffTests.exe" | tail -1
cd "D:/a_my/project/Forks/AltTaber/repo"
git status --porcelain
git log --oneline -4
```

Expected: `build_exit=0`, no errors, warning count `0`, both tests exit 0, `git status` empty, and the three `refactor:` commits visible.

- [ ] **Step 2: Record the result in `HANDOFF.md`**

Append a short section at the end of `D:\a_my\project\Forks\AltTaber\HANDOFF.md`:

```markdown
## 2026-10-18 简化收尾

- 删除 16 个无调用 API：`setWindowBlur`、`AppUtil::getAppFrameWindow`、`Startup::set`、
  `WindowWatcher::isRunning`、`WindowActivationOrder::clear`、`ConfigManagerBase::remove`、
  `GroupWindowList::count` / `currentRow`、`DwmThumbnail::source`，以及 UIAutomation/UIElement
  的 7 个未用 helper。
- `WindowActivationOrder::sortByActiveOrder()` 收为 private；`SystemTray` 启动项末尾的
  U+1F511 emoji 换成文字 `(admin)`。
- `WindowGeometry::groupContent()` 成为 GroupMode 内容尺寸/列表位置的唯一来源；
  `Widget::thumbnailActive_` 取代布局里的局部 `thumbOn`，窄屏隐藏缩略图后不会因换高亮又被放回来。
- `build.bat` 删除，`rebuild.bat` 负责在缺少 `build/` 时创建它；构建入口只剩一个。
- 验证：`rebuild.bat` exit 0、`warning C` 0；`WindowGeometryTests` 全绿、`WindowDiffTests` 18/18。
- 保留：`Startup.h` 的 `#if __has_include("ScheduledTask.h")` 回退（用户 2026-10-18 决定）。
```

- [ ] **Step 3: No commit for HANDOFF**

`HANDOFF.md` is outside the git repository. Leave it uncommitted; the repo commits from Tasks 1–3 are the deliverable.

---

## Self-Review

- **Spec coverage:** Task 1 covers the dormant utility/API batch, `DwmThumbnail::source`, the `sortByActiveOrder` encapsulation, and the tray emoji; Task 2 covers the UIAutomation cluster; Task 3 covers the group layout split truth and narrow-screen state; Task 4 covers `build.bat`; Task 5 records the result. The `Startup.h` fallback is explicitly preserved.
- **Placeholder scan:** every code step contains the actual code or exact deletion block; no TBD/TODO.
- **Type consistency:** `WindowGeometry::GroupContent` fields (`contentWidth`, `contentHeight`, `listX`, `listY`, `thumbnailActive`) are used with those exact names in tests and Widget; `thumbnailActive_` is the Widget member; `groupContent` signature matches all call sites.
