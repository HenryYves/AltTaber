# AltTaber 配置指南

`config.ini` 放在 `AltTaber.exe` 同级目录下。首次点击托盘菜单 **Settings** 时会自动生成。保存后字体设置立即生效；显示与切换类设置在下一次切换器弹窗时生效。

---

## 完整示例

```ini
DisplayMonitor=1

[monitor]
switch_mode=0

[label]
font_size=10
font_family=Microsoft YaHei UI

[group_list]
font_size=13
font_family=方正清刻本悦宋简体
thumbnail=true
thumbnail_width=320
thumbnail_height=180

[blacklist]
file_names=obsidian.exe, slack.exe

[sorting]
new_app_second=true

[dynamic]
watch_new_windows=true
auto_select_new=true
```

---

## 全部选项

### 显示显示器

切换面板显示在哪个屏幕上。

| 键 | 类型 | 默认值 | 说明 |
|-----|------|--------|------|
| `DisplayMonitor` | int | `1` | `0` = 主显示器，`1` = 跟随鼠标光标 |

也可以右键托盘图标 → **Display Monitor** 直接切换。

---

### `[monitor]` — 多显示器切换行为

在切换器里选定窗口时，其它屏幕上的窗口如何处理。

| 键 | 类型 | 默认值 | 取值 |
|-----|------|--------|------|
| `switch_mode` | int | `0` | `0` = 显示全部窗口并保持在原位；`1` = 只显示当前屏幕上的窗口；`2` = 显示全部窗口，并把选中的窗口移动到当前屏幕 |

```ini
[monitor]
switch_mode=0
```

**当前屏幕**指本次弹窗渲染的那块屏幕 —— 即切换器打开时 `DisplayMonitor` 决定的目标屏幕（主显示器，或鼠标光标下的显示器），并在本次弹出期间固定不变。

- `0`（默认）—— 所有窗口都列出，切换时不会移动任何窗口。
- `1` —— 应用列表与 Alt+`` ` `` 窗口列表都只保留位于当前屏幕的窗口。若某个应用组在当前屏幕上没有窗口，它的窗口列表不会弹出。切换器打开期间新发现的窗口也按屏幕过滤；窗口自己移到别的屏幕不追踪，下次弹窗重新计算。
- `2` —— 所有窗口都列出。提交选择时（在应用列表里松开 `Alt`，或在 Alt+`` ` `` 窗口列表里选定），先把目标窗口移动到当前屏幕：
  - 窗口保持原有**物理尺寸**，在当前屏幕的工作区居中；窗口大于工作区时贴工作区的左上角，不缩小；
  - 最大化窗口先还原，移动到目标屏幕后再重新最大化；
  - 已经位于当前屏幕的窗口不动；
  - 跨不同缩放率的屏幕时交给 Windows 原生的 `WM_DPICHANGED` 路径处理 —— per-monitor DPI aware 的应用按新 DPI 重新布局，其余应用由 DWM 位图拉伸，AltTaber 不预先缩放窗口。

该键在每次打开切换器时读取，保存后**下次弹窗**即生效，不需要重启。

---

### `[label]` — 字体设置

控制图标下方应用名称的字体样式。

| 键 | 类型 | 默认值 | 说明 |
|-----|------|--------|------|
| `font_size` | int | `10` | 字体大小（磅） |
| `font_family` | string | `Microsoft YaHei UI` | 首选字体；加载失败时依次尝试 Microsoft YaHei → Consolas |

---

### `[group_list]` — Alt+`` ` `` 组窗口列表的字体与缩略图

控制 Alt+`` ` `` 弹出的竖排窗口列表：字号、字体，以及高亮窗口的实时预览。与 `[label]` 各自独立，互不影响。

| 键 | 类型 | 默认值 | 说明 |
|-----|------|--------|------|
| `font_size` | int | （不设置） | 字体大小（磅）。`≤0` 或非数字视为未设置 |
| `font_family` | string | （不设置） | 首选字体；加载失败时依次尝试 Microsoft YaHei UI → Microsoft YaHei → Consolas |
| `thumbnail` | bool | `false` | 在列表右侧显示高亮窗口的 DWM 实时缩略图 |
| `thumbnail_width` | int | `320` | 缩略图盒宽度（逻辑像素），钳位到 `80`–`2000` |
| `thumbnail_height` | int | `180` | 缩略图盒高度（逻辑像素），钳位到 `60`–`2000` |

**两个字体键都可以不写：都不写就沿用系统默认字体**，与不带这一节时完全一致。

```ini
[group_list]
font_size=13
font_family=方正清刻本悦宋简体
```

字号变大时**行高与宽度上限都会跟着放宽**，否则大字会被行高裁掉、长标题会过早省略：

- 行高 = `max(28, 字高 + 8)`
- 宽度上限 = `480 × (字高 ÷ 默认字体字高)`，再受目标屏幕可用宽度兜底

默认字体是 Microsoft YaHei UI 9pt（字高 15px），此时两者正好是 **28** 与 **480** —— 也就是不配置时与改动前逐像素一致。

**缩略图预览** —— `thumbnail=true` 会在列表右侧显示当前高亮窗口的 DWM 实时预览，两者之间留 12px 间距。预览跟随选中项；快速移动时用 60ms 合并定时器刷新，逐行滚动不会闪。

```ini
[group_list]
thumbnail=true
thumbnail_width=320
thumbnail_height=180
```

- 只取源窗口的**客户区**，所以没有标题栏、也没有窗口边框。（应用自绘的标题栏属于客户区，会照常显示。）
- 预览按**等比缩放**放进 `thumbnail_width` × `thumbnail_height` 的盒子。盒子是上限而非强制尺寸：宽窗口的预览更矮，高窗口的预览更窄。盒子还会再受目标屏幕可用区域兜底，配置得再大也不会把弹窗顶出屏幕。
- 只在 Alt+`` ` `` **组列表**里显示，Alt+Tab 应用列表不受影响。
- **最小化窗口也显示预览**，这样列表仍能帮你认出最小化的窗口。被 DWM cloaked 的窗口、以及取不到客户区尺寸的源窗口不显示预览。
- 预览宿主与弹窗使用**同一套圆角**（Win11 走 DWM 圆角；旧版 Windows 回退 `SetWindowRgn`）。
- 屏幕窄到放不下「列表 + 预览」时，预览**整块隐藏**，绝不压住列表。
- `thumbnail` 为 `false` 时，`thumbnail_width` / `thumbnail_height` 不起作用。
- 默认 `thumbnail=false`：不注册预览、不预留空间，布局、尺寸与观感**与旧版逐像素一致**。

改完保存后下次弹窗生效，不用重启。

---

### `[blacklist]` — 隐藏不想看到的窗口

让指定应用不出现在 Alt+Tab 切换列表中。

| 键 | 类型 | 默认值 | 说明 |
|-----|------|--------|------|
| `file_names` | string | （空） | 逗号分隔的 exe 文件名，**大小写不敏感** |

```ini
[blacklist]
file_names=obsidian.exe, slack.exe, telegram.exe
```

> **注意**：以下三个进程硬编码隐藏，config.ini 无法恢复：
> `Nahimic3.exe`、`Follower.exe`、`QQ Follower.exe`。

如果配置了但应用仍然出现，去任务管理器确认准确的 exe 文件名。

---

### `[sorting]` — 排序规则

| 键 | 类型 | 默认值 | 说明 |
|-----|------|--------|------|
| `new_app_second` | bool | `false` | 启用后，没有被切换过的应用会排到第 2 位（紧跟当前前台窗口），而不是排在列表末尾。适用于通过快捷键启动但不自动获取焦点的应用。 |

```ini
[sorting]
new_app_second=true
```

---

### `[dynamic]` — 切换器显示期间的实时更新

两项都默认 `false`。`auto_select_new` 在 `watch_new_windows` 关闭时无效果。

| 键 | 类型 | 默认 | 说明 |
|-----|------|---------|-------------|
| `watch_new_windows` | bool | `false` | 切换器显示期间以 300ms 轮询新出现的后台窗口并插入列表；窗口关闭时对称移除。 |
| `auto_select_new` | bool | `false` | 新窗口入列时把选中项移到它上面。会让位于手动操作：本次弹出期间一旦按过 `` ` `` / 方向键 / Vim 方向键 / 滚轮（在应用列表里按 Tab 也算），就不再抢选中，直到下次弹出。 |

---

## 托盘菜单

以下设置通过右键托盘图标操作，不在 `config.ini` 中：

| 菜单项 | 功能 |
|--------|------|
| **Check for Updates** | 检查新版本 |
| **Settings** | 用记事本打开 `config.ini` |
| **Start with Windows** | 开机自启 |
| **Display Monitor** | 选择主显示器或跟随鼠标 |
| **Quit** | 退出 AltTaber |

---

## 快捷键

快捷键**不支持配置**：

| 快捷键 | 作用 |
|--------|------|
| `Alt` + `Tab` | 打开切换面板，向前切换应用 |
| `Alt` + `Shift` + `Tab` | 向后切换应用 |
| `Alt` + `` ` `` | 在同一应用的多个窗口间切换 |
| `H` `J` `K` `L` | Vim 方向键（← ↓ ↑ →） |
| `←` `→` | 左右移动选中项 |
| `↑` `↓` / 鼠标滚轮 | 在同一应用组内切换窗口 |
