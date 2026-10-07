# 组列表（Alt+`）样式修复 + 字号/字体配置

> 日期：2026-10-07
> 状态：**设计已确认，待实现**
> 仓库：`repo/`（fork `HenryYves/AltTaber`）
> 分支：`feat/group-window-list-and-live-scan`（起点 HEAD `1a83299`）
> 前置：2026-09-30 那轮加入的组列表功能，见 `2026-09-30-group-window-list-and-dynamic-scan-design.md`

---

## 0. 范围

做两件事：

1. **修复**：Alt+` 组列表里「选中行的文字比其它行右移 12px」
2. **新增**：`[group_list]` 配置节，让字号与字体可配置，且字号变大时行高自动跟随

**不做**：行高的独立配置键（自动派生够用）；粗体/斜体/颜色；`MinWidth 220 / MaxWidth 480` 这两条 clamp 的调整。

---

## 1. 已核实的事实（设计依据）

| 事实 | 依据 |
|---|---|
| 组列表是一个 `QListWidget`，外观全靠内联 QSS | `src/utils/GroupWindowList.cpp:17` |
| `::item` 上只有 padding（左右各 12px），`::item:selected` 上有背景色 + 圆角 | `src/utils/GroupWindowList.cpp:19-31` |
| 行高是**编译期常量** 28，被 5 处引用 | `header/utils/GroupWindowList.h:14`；`GroupWindowList.cpp:70,94,100,109` |
| 宽度已按 `list_->fontMetrics()` 自动计算，字号变化会**自动跟随** | `GroupWindowList.cpp:92` |
| 视图尺寸由 `Widget` 在展示时用 `preferredSize()` 设定 | `src/widget.cpp:441` |
| `GroupWindowList` 现在完全不认识配置（头文件无任何 config 依赖） | `header/utils/GroupWindowList.h` |
| `[label]` 字体的既有做法：直接在 `Widget` 里 `cfg.get(...)` + `configEdited` 热重载 | `src/widget.cpp:209-224` |
| 默认字体 = **Microsoft YaHei UI 9pt**（真机实测，非推断） | 见 §3.2 度量表 |

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

于是同一个 padding 只作用在一行上，看起来就是「选中项往右缩进」。

**已实测排除的替代假设**（都不是原因）：

| 假设 | 实测结果 |
|---|---|
| `border-radius: 4px` 导致 | 去掉后偏移仍是 +12 px |
| 需要在 `:selected` 里重写一遍 padding | 重写后偏移仍是 +12 px |

### 2.3 修法

给基础态补一句 `background-color: transparent;`，让两态都画盒子：

```css
QListWidget::item {
    color: rgb(240, 240, 240);
    background-color: transparent;   /* ← 新增。少了它，选中行文字右移 12px */
    padding-left: 12px;
    padding-right: 12px;
}
```

实测结果：两态文字都是 x=15，高亮仍是满宽 `[0, 319]`。

> 这行看着"多余"，删掉不会报任何错、只会让选中行悄悄右跳。所以源码里要留注释。

---

## 3. 新增：字号 / 字体配置

### 3.1 配置接口

```ini
[group_list]
font_size=12
font_family=方正清刻本悦宋简体
```

- 两个键**都可选**。**键不存在 → 完全不碰字体**，沿用继承来的默认字体（视觉零变化）
- `font_size` 是**磅值**（`setPointSize`），与 `[label]` 同语义
- `font_family` 是首选族名，后面接回退链 `{Microsoft YaHei UI, Microsoft YaHei, Consolas}`（与 `setupLabelFont()` 一致）
- 非法值（≤0、非数字）按「未设置」处理，避免出现 0pt 的不可见文字

### 3.2 行高随字号派生

`RowHeight` 从 `static constexpr` 改为成员 `rowHeight_`：

```
rowHeight_ = max(28, QFontMetrics(生效字体).height() + 8)
```

windows 平台实测值（`QT_QPA_PLATFORM=windows` 下跑 `metrics.exe` 得到）：

| 字号 | height | max(28, height+8) |
|---|---|---|
| 默认 9pt | 15 | **28** |
| 10pt | 16 | 28 |
| 11pt | 19 | 28 |
| 12pt | 20 | 28 |
| 14pt | 24 | 32 |
| 16pt | 26 | 34 |
| 20pt | 34 | 42 |

**默认字号下该公式恰好等于现在的 28**，所以不写配置时行高逐像素不变；≤12pt 也不虚胖。

实现上**只有一条路径**：行高总是由「生效字体」的度量派生，不存在「设了才派生、没设就用常量」的分支。两键都没设时生效字体就是默认字体，派生结果实测为 28。这样代码里没有第二套逻辑，也不会出现「只设了 `font_family` 时行高没跟上」的不一致。

宽度不需要处理：`preferredSize()` 本来就用 `list_->fontMetrics()` 算（`GroupWindowList.cpp:92`）。

### 3.3 代码边界

- `GroupWindowList` **保持不认识配置**（现在头文件里没有任何 config 依赖，这是个好边界）
  → 新增 `void applyFont(const QFont&)`：内部 `setFont` + 重算 `rowHeight_` + `applyPreferredWidth()`
- 读配置放 `Widget`，与 `[label]` 字体同款写法：构造时应用一次；`configEdited` 时再应用一次
  → **改完配置立即生效，不用重启**
- 若热重载发生时组列表恰好可见，顺带重设一次 `view->setFixedSize(preferredSize())`
  （`widget.cpp:441` 那条只在展示时跑，热重载不在那个时机）

---

## 4. 实现计划

| # | 步骤 | 判据 |
|---|---|---|
| 1 | `header/utils/GroupWindowList.h`：`RowHeight` → 成员 `rowHeight_`（保留 28 为下限常量）；新增 `applyFont(const QFont&)` | 编译通过；`RowHeight` 无残留引用 |
| 2 | `src/utils/GroupWindowList.cpp`：QSS 加 `background-color: transparent;` + 注释；5 处 `RowHeight` → `rowHeight_` | 探针量出两态文字 x 相等 |
| 3 | `src/widget.cpp`：构造时按 `[group_list]` 应用字体；`configEdited` 热重载 | 改配置后不重启即生效 |
| 4 | `CONFIG.md` / `CONFIG_zh.md`：各加 `[group_list]` 一节 | 与代码里的键名逐字一致 |
| 5 | 重新编译 → 部署 → 验收 | 见 §5 |

> **编译注意**：`build.bat` / `rebuild.bat` / `deploy.bat` 里写死的仓库路径仍是改名前的
> `2026-06-16_alttaber_research`，且仓库里 `build/CMakeCache.txt` 也还指着那个旧路径，
> **这三个 bat 现在直接跑会失败**。本轮改用源外构建（`cmake -S <repo> -B <临时目录>`）绕开；
> 修 bat 与 build 目录属于独立的收尾项，不在本轮范围内。

---

## 5. 验收

- [ ] **不写配置**：行高 28、文字位置、宽度与现在完全一致（重点回归）
- [ ] `font_size=14`：行高变 32、文字不裁、宽度跟随变宽、高亮仍满宽
- [ ] `font_family=方正清刻本悦宋简体`：生效
- [ ] `font_family` 写成不存在的名字：回退到 Microsoft YaHei UI，不崩
- [ ] 编辑 `config.ini` 保存后**不重启**即生效
- [ ] Alt+` 选中行的文字不再右移
- [ ] 既有单测 `WindowDiffTests` 18/18（GUI 覆盖不到，只当回归）

### 探针复现方法

本次用到的一次性探针（`%TEMP%\alttaber_qss_probe\`，未纳入版本控制）：CMake + Qt6 Widgets 建两个小程序——

- `qss_probe.exe`：逐个 QSS 变体渲染 `QListWidget`，逐行扫描「最左侧浅色像素」得到文字起始 x、并输出 PNG 与 `probe_result.txt`
- `metrics.exe`：打印 `QApplication::font()`、各字号的 `QFontMetrics::height()`

**两个坑（都踩过）**：

1. **离屏平台的字体度量与真机不同** —— 所以行高公式的真值必须来自 `QT_QPA_PLATFORM=windows` 的 `metrics.exe`，不能用 `offscreen` 的数字
2. 手工用 `cl.exe` 编译 Qt 6.8.1 会撞上 `/Zc:__cplusplus` 与 `compareThreeWay` 重载冲突 —— 一律走 CMake（`find_package(Qt6)` 带齐 flag）

---

## 6. 已知限制

- 字号很大时（约 >18pt），长标题仍会被 `MaxWidth=480` clamp 后右省略 —— 既有行为，本轮不改
- GUI 部分没有自动化测试覆盖（本仓只有 `WindowDiffTests` 覆盖纯逻辑类），仍靠离屏探针 + 真机目视
- `[dynamic]` 动态扫描仍未验收，与本轮无关
