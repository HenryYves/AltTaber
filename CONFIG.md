# AltTaber Configuration Guide

`config.ini` lives next to `AltTaber.exe`. It is created automatically when you click **Settings** in the tray icon menu. Saving the file applies font settings immediately; display and switching settings take effect on the next switcher popup.

---

## Complete example

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

## All options

### Display Monitor

Which screen the switcher appears on.

| Key | Type | Default | Values |
|-----|------|---------|--------|
| `DisplayMonitor` | int | `1` | `0` = primary monitor, `1` = follow mouse cursor |

Can also be changed via tray icon → **Display Monitor**.

---

### `[monitor]` — Multi-monitor switching behavior

What happens to windows on other screens when a window is picked in the switcher.

| Key | Type | Default | Values |
|-----|------|---------|--------|
| `switch_mode` | int | `0` | `0` = show all windows and keep them where they are; `1` = show only windows on the current screen; `2` = show all windows and move the selected window to the current screen |

```ini
[monitor]
switch_mode=0
```

**Current screen** means the screen this popup is rendered on — the screen `DisplayMonitor` resolves to when the switcher opens (primary monitor, or the monitor under the mouse cursor), pinned for the duration of that popup.

- `0` (default) — every window is listed; switching never moves a window.
- `1` — both the app list and the Alt+`` ` `` window list keep only windows located on the current screen. If a group has no window on the current screen, its window list does not open. New windows discovered while the switcher is open are also filtered by screen; a window that moves to another screen is picked up on the next popup.
- `2` — every window is listed. When the selection is committed (releasing `Alt` in the app list, or picking in the Alt+`` ` `` window list), the target window is moved to the current screen first:
  - the window keeps its **physical** size and is centered on the current screen's work area; if it is larger than the work area it is pinned to the work area's top-left corner instead of being shrunk;
  - a maximized window is restored first, moved, then maximized again on the target screen;
  - a window already on the current screen is left alone;
  - moving between screens with different scale factors is left to Windows' native `WM_DPICHANGED` path — per-monitor DPI-aware apps re-lay out at the new DPI, other apps are bitmap-stretched by DWM. AltTaber does not pre-scale the window.

The key is read whenever the switcher opens, so a saved change takes effect on the **next popup**; no restart is needed.

---

### `[label]` — Font customization

Controls the label text shown under each app icon.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `font_size` | int | `10` | Font point size |
| `font_family` | string | `Microsoft YaHei UI` | Preferred font; falls back to Microsoft YaHei → Consolas |

---

### `[group_list]` — Font and thumbnails for the Alt+`` ` `` window list

Controls the vertical window list shown by Alt+`` ` ``: its font, and the live preview of the highlighted window. Independent of `[label]`.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `font_size` | int | (unset) | Font point size. `<=0` or non-numeric counts as unset |
| `font_family` | string | (unset) | Preferred font; falls back to Microsoft YaHei UI → Microsoft YaHei → Consolas |
| `thumbnail` | bool | `false` | Show a live DWM preview of the highlighted window to the right of the list |
| `thumbnail_width` | int | `320` | Width of the preview box in logical pixels; clamped to `80`–`2000` |
| `thumbnail_height` | int | `180` | Height of the preview box in logical pixels; clamped to `60`–`2000` |

**Both font keys are optional. With neither set, the system default font is used**, which is identical to not having this section at all.

```ini
[group_list]
font_size=13
font_family=方正清刻本悦宋简体
```

When the font grows, **row height and width cap grow with it** — otherwise large text would be clipped by the row and long titles would elide too early:

- row height = `max(28, font height + 8)`
- width cap = `480 × (font height ÷ default font height)`, then clamped to the target screen's available width

The default font is Microsoft YaHei UI 9pt (height 15px), which makes both exactly **28** and **480** — i.e. leaving this unset changes nothing pixel-wise.

**Thumbnail preview** — set `thumbnail=true` to show a live DWM preview of the window currently highlighted in the list, to its right (separated by a 12 px gap). The preview follows the selection; rapid moves are coalesced on a 60 ms timer so scrolling through rows does not flicker.

```ini
[group_list]
thumbnail=true
thumbnail_width=320
thumbnail_height=180
```

- Only the **client area** of the source window is captured — no title bar and no window border. (A title bar drawn by the app itself is part of the client area and will show.)
- The preview is scaled **proportionally** to fit inside the `thumbnail_width` × `thumbnail_height` box. The box is an upper bound, not a forced size, so wide windows get a shorter preview and tall windows a narrower one. The box itself is further clamped to the target screen's available area, so an oversized configuration cannot push the popup off-screen.
- It is shown **only in the Alt+`` ` `` group list**; the Alt+Tab app list is unaffected.
- **Minimized windows also get a preview**, so the list still helps you recognize windows that are minimized. DWM-cloaked windows, and sources whose client area cannot be queried, hide the preview.
- The preview host gets the **same rounded corners as the popup** (Win11 DWM corner preference; `SetWindowRgn` fallback on older Windows).
- If the screen is too narrow to fit the list plus the preview, the preview is **hidden entirely** — it never overlaps the list.
- `thumbnail_width` and `thumbnail_height` have no effect while `thumbnail` is `false`.
- With `thumbnail=false` (the default), no preview is registered and no space is reserved: layout, sizes and appearance are **pixel-identical to previous versions**.

Saved changes apply on the next popup; no restart needed.

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

### `[dynamic]` — Live update while the switcher is open

Both default to `false`. `auto_select_new` has no effect while `watch_new_windows` is off.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `watch_new_windows` | bool | `false` | While the switcher is open, poll for newly appeared background windows at 300 ms and insert them into the list. Closed windows are removed symmetrically. |
| `auto_select_new` | bool | `false` | When a new window is inserted, move the selection onto it. Yields to manual navigation: once you press `` ` `` / an arrow / a Vim arrow / the wheel during the current popup (Tab too, while in the app list), the selection stops being taken over until the next popup. |

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
