# AltTaber Configuration Guide

`config.ini` lives next to `AltTaber.exe`. It is created automatically when you click **Settings** in the tray icon menu. Changes take effect after restarting AltTaber.

---

## Complete example

```ini
DisplayMonitor=1
[label]
font_size=10
font_family=Microsoft YaHei UI

[blacklist]
file_names=obsidian.exe, slack.exe

[sorting]
new_app_second=true
```

---

## All options

### Display Monitor

Which screen the switcher appears on.

| Key | Type | Default | Values |
|-----|------|---------|--------|
| `DisplayMonitor` | int | `1` | `0` = primary monitor, `1` = follow mouse cursor |

Can also be changed via tray icon → **Display Monitor**.

---

### `[label]` — Font customization

Controls the label text shown under each app icon.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `font_size` | int | `10` | Font point size |
| `font_family` | string | `Microsoft YaHei UI` | Preferred font; falls back to Microsoft YaHei → Consolas |

---

### `[blacklist]` — Hide apps from the switcher

Prevent specific apps from appearing in the Alt+Tab list.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `file_names` | string | (empty) | Comma-separated `.exe` names, **case-insensitive** |

```ini
[blacklist]
file_names=obsidian.exe, slack.exe, telegram.exe
```

> **Note:** Three apps are always hidden regardless of `config.ini`:
> `Nahimic3.exe`, `Follower.exe`, `QQ Follower.exe`.
> These cannot be un-hidden.

If an app still shows up, check its exact exe name via Task Manager.

---

### `[sorting]` — Sort order

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `new_app_second` | bool | `false` | When enabled, apps that have never been activated are placed at position 2 (right after the foreground window) instead of at the end of the list. Useful for apps launched via hotkey that don't grab focus on startup. |

```ini
[sorting]
new_app_second=true
```

---

## Tray icon menu

These settings are managed through the tray menu, not `config.ini`:

| Menu item | What it does |
|-----------|--------------|
| **Check for Updates** | Check for new AltTaber releases |
| **Settings** | Open `config.ini` in Notepad for editing |
| **Start with Windows** | Toggle auto-start with Windows |
| **Display Monitor** | Choose primary monitor or follow mouse |
| **Quit** | Exit AltTaber |

---

## Keyboard shortcuts

These are **not configurable**:

| Shortcut | Action |
|----------|--------|
| `Alt` + `Tab` | Open switcher, cycle through apps forward |
| `Alt` + `Shift` + `Tab` | Cycle through apps backward |
| `Alt` + `` ` `` | Cycle through windows of the same app |
| `H` `J` `K` `L` | Vim-style navigation (← ↓ ↑ →) |
| `←` `→` | Move selection left / right |
| `↑` `↓` / mouse wheel | Scroll within an app group (cycle its windows) |
