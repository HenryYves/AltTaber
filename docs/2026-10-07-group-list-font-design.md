# AltTaber 组列表（Alt+`）本轮改动：样式修复 · 字号字体 · 两个方向的行为

> 日期：2026-10-07
> 状态：**四件改动全部已实现并编译通过**（工作树 commit 见 HANDOFF.md）；部署与真机验收状态见 §7
> 仓库：`repo/`（fork `HenryYves/AltTaber`）
> 位置：本轮把开发文件夹迁到了 `D:\a_my\project\Forks\AltTaber\`（见 §9）
> 分支：`feat/group-window-list-and-live-scan`
> 前置：2026-09-30 那轮加入的组列表功能，见 `2026-09-30-group-window-list-and-dynamic-scan-design.md`

---

## 0. 范围

四件事：

1. **修复**：Alt+`` ` `` 组列表里「选中行的文字比其它行右移 12px」
2. **新增**：`[group_list]` 配置节（`font_size` / `font_family`），且字号变大时**行高与宽度上限一起放宽**
3. **改行为**：入口 B（切换器已可见时按 `` ` ``）从「高亮项所在应用」改为「**出发应用**」
4. **改行为**：组列表里按 Tab 或 `` ` `` **回到应用切换器**（高亮停在组列表所属的那个应用）

**不做**：行高的独立配置键（按字号派生够用）；粗体/斜体/颜色；`MinWidth 220` 这个地板随字号变（它只是下限，不参与裁字）。

---

## 1. 已核实的事实（设计依据，行号均为**改动后**）

| 事实 | 依据 |
|---|---|
| 组列表是一个 `QListWidget`，外观全靠内联 QSS | `src/utils/GroupWindowList.cpp:18` |
| `::item` 上只有 padding（左右各 12px），`::item:selected` 上有背景色 + 圆角 | `GroupWindowList.cpp:24-38` |
| 行高原本是**编译期常量** 28；宽度上限原本是常量 480 | `header/utils/GroupWindowList.h:14,16`（改动前） |
| 宽度按 `list_->fontMetrics()` 算，**字号一变宽度自动跟随**；上限是唯一不跟随的部分 | `GroupWindowList.cpp:115-123` |
| 视图尺寸由 `Widget` 展示时用 `preferredSize()` 设定，只居中、**不兜宽度** | `src/widget.cpp:478` 附近；`layoutForCurrentMode()` |
| 切换器可见时 `GetForegroundWindow()` 返回本 Widget 自己 | `widget.cpp:757` 的注释；`main.cpp:65` 同类说明 |
| 组内窗口按最后使用时间**降序**（MRV 在第 0 行） | `src/utils/WindowActivationOrder.cpp:37`（`>` 比较） |
| `[label]` 字体的既有做法：`Widget` 里直接 `cfg.get(...)` + `configEdited` 热重载 | `widget.cpp:209-236` |
| 应用默认字体 = **Microsoft YaHei UI 9pt，height=15px**（真机实测） | §3.2 度量表 |

---

## 2. 修复：选中行文字右移 12px

### 2.1 现象与量化

离屏探针逐行扫描「最左侧浅色文字像素」（方法见 §5）：

| 版本 | 未选中行文字最左 x | 选中行文字最左 x | 偏移 |
|---|---|---|---|
| 修复前 | 3 | 15 | **+12 px** |
| 修复后 | 15 | 15 | 0 |

高亮底色范围两版都是 `[0, 319]`（满宽）——**问题不在高亮条**，只在文字。

### 2.2 根因

Qt 的 QSS 规矩：`::item` 上的 `padding-*` **只有当该选择器命中、且真的画了盒子时才会被套用**。

- 未选中态：没有任何背景/边框 → 不画盒子 → **padding 被完全忽略**（文字 x=3）
- 选中态：有 `background-color` → 画盒子 → padding 生效（文字 x=15）

同一个 padding 只作用在一行上，看起来就是「选中项往右缩进」。

**已实测排除的替代假设**（都不是原因）：去掉 `:selected` 的 `border-radius` 后偏移仍是 +12；在 `:selected` 里重写一遍同样的 padding 后仍是 +12。

### 2.3 修法

基础态补一句 `background-color: transparent;`，让两态都画盒子（`GroupWindowList.cpp:24-32`）。实测两态文字都是 x=15，高亮仍满宽。源码里留了注释——**删掉它不会报任何错，只会让选中行悄悄右跳**。

---

## 3. 新增：`[group_list]` 字号 / 字体

### 3.1 配置接口

```ini
[group_list]
font_size=13
font_family=方正清刻本悦宋简体
```

- 两个键**都可选**。**都不写 → 完全不碰字体**，沿用继承来的默认字体
- `font_size` 是**磅值**（`setPointSize`），与 `[label]` 同语义；`≤0` 或非数字按未设置处理（不给 0pt 的不可见字）
- `font_family` 是首选族名，后接回退链 `{Microsoft YaHei UI, Microsoft YaHei, Consolas}`
- 生效时机：构造时一次 + `configEdited` 时重应用 → **改完保存立即生效，不用重启**

### 3.2 行高按字号派生

```
rowHeight_ = max(28, QFontMetrics(生效字体).height() + 8)
```

真机实测（`QT_QPA_PLATFORM=windows`，见 §5）：

| 字体 | 字号 | height | 行高 |
|---|---|---|---|
| Microsoft YaHei UI（默认） | 9pt | 15 | **28** |
| 同上 | 12pt | 20 | 28 |
| 同上 | 14pt | 24 | 32 |
| 同上 | 20pt | 34 | 42 |
| 方正清刻本悦宋简体 | 13pt | 19 | **28** |
| 方正清刻本悦宋简体 | 16pt | 23 | 31 |
| 方正清刻本悦宋简体 | 20pt | 29 | 37 |

**默认字体下该公式恰好等于现在的 28**，所以不写配置时行高逐像素不变；≤14pt 也不虚胖（同磅值下不同字体高度不同，方正这套明显比雅黑矮）。

实现上**只有一条路径**：行高总是由「生效字体」的度量派生，不存在「设了才派生、没设就用常量」的分支。

### 3.3 宽度上限随字号放宽（否则长标题会过早省略）

```
effectiveMaxWidth = qMax(MinWidth, min(round(480 × 字高 ÷ 默认字体字高), 屏幕可用宽度))
```

基准取 `QFontMetrics(QApplication::font()).height()` 而**不是写死 15**：默认字体下比值恒为 1.0 ⇒ 上限仍是 480（不变量），且 100%/125%/150% DPI 下自动成立。

屏幕兜底那一层必须有：`layoutForCurrentMode()` 只做居中、不兜宽度，超宽会被裁在屏幕两侧。由 `Widget` 在布局前用 `setWidthCap()` 设（`widget.cpp:474-478`），它顺带重排每一行的 `sizeHint`（只改视图尺寸会让高亮条比视图窄）。

实测（方正清刻本悦宋简体）：

| 字号 | height | 行高 | 缩放 = height/15 | 宽度上限 |
|---|---|---|---|---|
| 9pt | 13 | 28 | 0.867 | 416 |
| 13pt | 19 | 28 | 1.267 | **608** |
| 14pt | 20 | 28 | 1.333 | 640 |
| 16pt | 23 | 31 | 1.533 | 736 |
| 20pt | 29 | 37 | 1.933 | 928 |

> 注意缩放**可以小于 1**：选了比默认字体更矮的字体（上表 9pt 那行）时上限会收窄，这是"跟随实际字高"的必然结果，不配置时仍是 480。

### 3.4 代码边界

- `GroupWindowList` **保持不认识配置**（头文件里没有任何 config 依赖）→ 新增 `applyFont(const QFont&)`、`setWidthCap(int)`（`header/utils/GroupWindowList.h:29,33`）
- 读配置在 `Widget::setupGroupListFont()`（`widget.cpp:239`），与 `setupLabelFont()` 同款；字体基准在构造时取一次，避免反复 apply 时拿上一次的结果当基准

---

## 4. 改行为：入口 B 用「出发应用」

### 4.1 改动

| | 改动前 | 改动后 |
|---|---|---|
| 组列表用哪个进程 | `lw->currentItem()`（**高亮项**） | 切换器弹出那一刻的前台窗口（**出发应用**） |

- 记录点：`requestShow()` 里，**仅当本窗口还不可见**时记 `GetForegroundWindow()`，且要求 `Util::isWindowAcceptable()`（`widget.cpp:527-531`）。
  可见性守卫是必须的：`requestShow()` 会被 main.cpp 的重试循环与"重复按 Alt+Tab"调用，那时前台已经是本 Widget 自己。
- 使用点：`requestShowGroup()` 的入口 B 分支（`widget.cpp:753-775`）
- **回退**：出发窗口在此期间被关掉/被过滤 → 解析不出进程路径 → 退回「高亮项」这份更老的定义，保证按了 `` ` `` 不会没反应

### 4.2 不变式仍然成立

入口 B 原本有「按 `` ` `` 再松手 == 什么都不做」的不变式（`design.md:59`）。换成出发应用后依然成立：你刚从那个窗口过来 ⇒ 它就是该组的 MRU ⇒ 落在第 0 行 ⇒ 松手切回它 = 原地不动。

### 4.3 取舍（已与用户确认）

**从切换器里不再能看到"高亮应用"的窗口列表。** 要在那儿切窗口，先松手切过去，再按 Alt+`` ` ``（那时它就是前台，走入口 A）。

---

## 5. 改行为：组列表里 Tab / `` ` `` 回应用切换器

改动前 GroupMode 下 Tab 与 `` ` `` 都是"组内移动高亮"（`groupNavDeltaOf()`）。现在两者都**回到应用切换器**，高亮停在**组列表所属的那个应用**上（`exitGroupModeToApp()`，`widget.cpp:819`）。

- 于是得到与入口 B 对称的不变式：**按 Tab/`` ` `` 再松手 == 原地不动**
- 组内导航因此只剩**方向键 / Vim HJKL / 滚轮**；`groupNavDeltaOf()` 里的 `Qt::Key_Tab` 已删除（真落进去则 delta=0，什么都不做，是安全的默认）
- AppMode 里的 Tab 不变，仍是"下一个应用"
- 找不到对应项（该应用的窗口都关了）时保持原来的高亮

---

## 6. 实现计划（四步全绿）

| # | 步骤 | 判据 | 状态 |
|---|---|---|---|
| 1 | `GroupWindowList`：QSS 修复；`RowHeight` → 成员 `rowHeight_`；新增 `applyFont` / `setWidthCap` / `effectiveMaxWidth` | 编译通过；探针量出两态文字 x 相等 | 完成 |
| 2 | `Widget::setupGroupListFont()`：读 `[group_list]`，构造时应用 + `configEdited` 热重载 | 改配置不重启即生效 | 完成 |
| 3 | 入口 B 改用 `foregroundAtShow_`，带高亮项回退 | 见 §7 验收 | 完成 |
| 4 | GroupMode 的 Tab / `` ` `` → `exitGroupModeToApp()` | 见 §7 验收 | 完成 |
| 5 | `CONFIG.md` / `CONFIG_zh.md` 加 `[group_list]`；修 `[dynamic]` 里过时的按键清单 | 与代码里的键名逐字一致 | 完成 |

编译：`rebuild.bat` 全量通过、**无 warning**；既有单测 `WindowDiffTests` **18/18**。

---

## 7. 验收

**自动化能覆盖的（已跑）**

- [x] 编译通过、无 warning；单测 18/18
- [x] 新 exe 二进制内含 `group_list/font_size`、`group_list/font_family`
- [x] 离屏探针：QSS 修复前后两态文字 x（3/15 → 15/15）
- [x] 真机度量：行高与宽度上限的派生值（§3.2、§3.3 两张表）

**只能人工/真机确认的（待用户）**

- [ ] **不写配置**：行高 28、宽度上限 480、观感与改动前一致（回归）
- [ ] `[group_list] font_size=13` + `方正清刻本悦宋简体`：字变大、行高仍 28、文字不裁、长标题不再过早省略
- [ ] 字体名写成不存在的：回退到 Microsoft YaHei UI，不崩
- [ ] 编辑保存后**不重启**即生效
- [ ] Alt+`` ` `` 选中行文字不再右移
- [ ] 应用 A 里按 Alt+Tab 弹出切换器 → 高亮移到 B → 按 `` ` `` → **显示 A 的窗口列表**（入口 B 新语义）
- [ ] 在组列表里按 Alt+Tab → **回到应用切换器**，高亮停在 A；松手 = 原地不动
- [ ] 在组列表里按 `` ` `` → 同上
- [ ] 组列表里方向键 / Vim HJKL / 滚轮仍能移动高亮

### 探针复现方法

一次性探针在 `%TEMP%\alttaber_qss_probe\`（**未纳入版本控制**）：CMake + Qt6 Widgets 建三个小程序——`qss_probe.exe` 逐 QSS 变体渲染并扫像素、`metrics.exe` / `metrics2.exe` 打印字体度量与派生值。

**两个坑（都踩过）**：

1. **离屏平台的字体度量与真机不同** —— 行高/宽度公式的真值必须来自 `QT_QPA_PLATFORM=windows`，不能用 `offscreen` 的数字
2. 手工用 `cl.exe` 编译 Qt 6.8.1 会撞上 `/Zc:__cplusplus` 与 `compareThreeWay` 重载冲突 —— 一律走 CMake

---

## 8. 已知限制

1. 从切换器里看不到"高亮应用"的窗口列表（§4.3，取舍）
2. 出发窗口被关掉时，入口 B 会回退到"跟着高亮走"——同一按键在边界情形下语义不一致
3. 宽度上限在极端字号下会被屏幕可用宽度兜住（这是有意的兜底，不是 bug）
4. GUI 部分没有自动化测试覆盖（本仓只有 `WindowDiffTests` 覆盖纯逻辑类），仍靠离屏探针 + 真机目视
5. `[dynamic]` 动态扫描仍未验收，与本轮无关

---

## 9. 开发文件夹迁移（2026-10-07）

`2026-06-16_alttaber` 从 Daily_Claude 工作区迁到 `D:\a_my\project\Forks\AltTaber\`，并去掉日期前缀——它已经不是临时任务了。`Forks\` 的分类轴是**所有权**（别人仓库 fork/克隆来的），而不是"是不是 git 仓库"（那个判据很快对自建项目也成立）。

一并处理：

- `build.bat` / `rebuild.bat` / `deploy.bat` 里写死的旧路径 → 新路径（三个 bat 原本指向改名前的 `..._alttaber_research`，早就跑不了）
- `repo/build/CMakeCache.txt` 里同样指着旧路径（`--fresh` 重配解决）
- 迁移前那份构建产物归档为 `AltTaber-2026-09-30.exe`（hash `9def6318…`），因为重建会覆盖 `repo/build/Release/AltTaber.exe`
- `HANDOFF.md` 里的绝对路径同步更新
