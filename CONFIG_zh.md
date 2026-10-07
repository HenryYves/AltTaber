# AltTaber 配置指南

`config.ini` 放在 `AltTaber.exe` 同级目录下。首次点击托盘菜单 **Settings** 时会自动生成。修改后重启 AltTaber 生效。

---

## 完整示例

```ini
DisplayMonitor=1
[label]
font_size=10
font_family=Microsoft YaHei UI

[group_list]
font_size=13
font_family=方正清刻本悦宋简体

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

### `[label]` — 字体设置

控制图标下方应用名称的字体样式。

| 键 | 类型 | 默认值 | 说明 |
|-----|------|--------|------|
| `font_size` | int | `10` | 字体大小（磅） |
| `font_family` | string | `Microsoft YaHei UI` | 首选字体；加载失败时依次尝试 Microsoft YaHei → Consolas |

---

### `[group_list]` — Alt+`` ` `` 组窗口列表的字体

控制 Alt+`` ` `` 弹出的竖排窗口列表的字号与字体。与 `[label]` 各自独立，互不影响。

| 键 | 类型 | 默认值 | 说明 |
|-----|------|--------|------|
| `font_size` | int | （不设置） | 字体大小（磅）。`≤0` 或非数字视为未设置 |
| `font_family` | string | （不设置） | 首选字体；加载失败时依次尝试 Microsoft YaHei UI → Microsoft YaHei → Consolas |

**两个键都可以不写：都不写就沿用系统默认字体**，与不带这一节时完全一致。

```ini
[group_list]
font_size=13
font_family=方正清刻本悦宋简体
```

字号变大时**行高与宽度上限都会跟着放宽**，否则大字会被行高裁掉、长标题会过早省略：

- 行高 = `max(28, 字高 + 8)`
- 宽度上限 = `480 × (字高 ÷ 默认字体字高)`，再受目标屏幕可用宽度兜底

默认字体是 Microsoft YaHei UI 9pt（字高 15px），此时两者正好是 **28** 与 **480** —— 也就是不配置时与改动前逐像素一致。

改完保存即生效，不用重启。

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
| `auto_select_new` | bool | `false` | 新窗口入列时把选中项移到它上面。会让位于手动操作：本次弹出期间一旦按过方向键 / Vim 方向键 / 滚轮（在应用列表里按 Tab 也算），就不再抢选中，直到下次弹出。 |

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
