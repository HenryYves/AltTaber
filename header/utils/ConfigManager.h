#ifndef WIN_SWITCHER_CONFIGMANAGER_H
#define WIN_SWITCHER_CONFIGMANAGER_H

#include <QSettings>
#include <QApplication>
#include "ConfigManagerBase.h"

// 注意：对于大量使用的类，header-only 模式会导致编译时间过长
#define cfg ConfigManager::instance()

enum DisplayMonitor {
    PrimaryMonitor, // 0 主显示器
    MouseMonitor, // 1 跟随鼠标
    EnumCount // Just for count
};

/// 切换器显示时，其余屏幕的行为
/// In config.ini: [monitor] \n switch_mode=0|1|2
enum MonitorSwitchMode {
    AllKeepPosition = 0, // 全部留在原屏幕
    CurrentOnly = 1, // 只在当前屏幕显示
    AllMoveToCurrent = 2 // 全部移动到当前屏幕
};

/// 切换器背景：系统 DWM 模糊、自绘截图模糊、纯色半透明，或按系统「透明效果」自动选择
/// In config.ini: [appearance] \n background=auto|system|self|tint
enum BackgroundMode {
    BackgroundAuto, // 系统透明开启 → system；关闭 → self
    BackgroundSystem, // SetWindowCompositionAttribute blur
    BackgroundSelf, // 截图 + 程序内 blur
    BackgroundTint // 只画半透明颜色
};

class ConfigManager : public ConfigManagerBase {
    inline static const QString FileName = "config.ini";

public:
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    static ConfigManager& instance() {
        static const auto filePath = QApplication::applicationDirPath() + "/" + FileName;
        static ConfigManager instance{filePath}; // multiple threads safe
        return instance;
    }

public:
    DisplayMonitor getDisplayMonitor() {
        auto monitor = get("DisplayMonitor", DisplayMonitor::MouseMonitor).toInt();
        if (monitor < 0 || monitor >= DisplayMonitor::EnumCount) {
            qWarning() << "Invalid DisplayMonitor enum" << monitor;
            monitor = DisplayMonitor::MouseMonitor;
        }
        return static_cast<DisplayMonitor>(monitor);
    }

    void setDisplayMonitor(DisplayMonitor monitor) {
        set("DisplayMonitor", monitor);
    }

    /// 多屏切换行为，越界值回退到 AllKeepPosition
    MonitorSwitchMode getMonitorSwitchMode() {
        const auto raw = get("monitor/switch_mode", static_cast<int>(MonitorSwitchMode::AllKeepPosition));
        bool ok = false;
        const int mode = raw.toString().toInt(&ok);
        // 非整数字符串（例如 `current`）以前会经 toInt() 静默变成 0，看起来像配对了；
        // 现在显式告警并回退（s02 回执 2026-10-17）
        if (!ok || mode < MonitorSwitchMode::AllKeepPosition || mode > MonitorSwitchMode::AllMoveToCurrent) {
            qWarning() << "Invalid monitor/switch_mode" << raw << "— falling back to 0 (all screens, keep position)";
            return MonitorSwitchMode::AllKeepPosition;
        }
        return static_cast<MonitorSwitchMode>(mode);
    }

    /// 组列表右侧的 DWM 实时缩略图预览，仅 GroupMode 生效。
    /// In config.ini: [group_list] \n thumbnail=true
    bool getGroupThumbnailEnabled() {
        return get("group_list/thumbnail", false).toBool();
    }

    /// 缩略图预览盒宽度（逻辑像素），默认 320，钳位 80..2000。
    /// In config.ini: [group_list] \n thumbnail_width=320
    int getGroupThumbnailWidth() {
        bool ok = false;
        const int width = get("group_list/thumbnail_width", 320).toInt(&ok);
        return qBound(80, ok ? width : 320, 2000);
    }

    /// 缩略图预览盒高度（逻辑像素），默认 180，钳位 60..2000。
    /// In config.ini: [group_list] \n thumbnail_height=180
    int getGroupThumbnailHeight() {
        bool ok = false;
        const int height = get("group_list/thumbnail_height", 180).toInt(&ok);
        return qBound(60, ok ? height : 180, 2000);
    }

    /// 背景模式。auto 读取 HKCU\...\Themes\Personalize\EnableTransparency：
    /// 开启时用系统模糊（观感最好），关闭时用自绘模糊（不依赖系统开关）。
    /// In config.ini: [appearance] \n background=auto
    BackgroundMode getBackgroundMode() {
        const auto raw = get("appearance/background", "auto").toString().trimmed().toLower();
        if (raw == "auto") return BackgroundAuto;
        if (raw == "system") return BackgroundSystem;
        if (raw == "self") return BackgroundSelf;
        if (raw == "tint") return BackgroundTint;
        qWarning() << "Invalid appearance/background" << raw << "— falling back to auto";
        return BackgroundAuto;
    }

    /// 自绘模式的模糊半径（逻辑像素），默认 32，钳位 1..100。
    int getBackgroundBlurRadius() {
        bool ok = false;
        const int radius = get("appearance/blur_radius", 32).toInt(&ok);
        return qBound(1, ok ? radius : 32, 100);
    }

    /// 自绘模式模糊前的缩放下采样倍数：1/2/4；越小越准、越慢。非法值回退 2。
    int getBackgroundBlurScale() {
        bool ok = false;
        const int scale = get("appearance/blur_scale", 2).toInt(&ok);
        if (!ok || (scale != 1 && scale != 2 && scale != 4)) {
            qWarning() << "Invalid appearance/blur_scale" << scale << "— falling back to 2";
            return 2;
        }
        return scale;
    }

    /// 叠加在背景上的颜色，`#RRGGBB`，默认 #141414。
    QString getBackgroundOverlayColor() {
        return get("appearance/overlay_color", "#141414").toString();
    }

    /// 叠加颜色的透明度 0..255，默认 140。
    int getBackgroundOverlayOpacity() {
        bool ok = false;
        const int opacity = get("appearance/overlay_opacity", 140).toInt(&ok);
        return qBound(0, ok ? opacity : 140, 255);
    }

    /// When enabled, apps with no usage record are sorted to the second position
    /// (right after the foreground window) instead of the end.
    /// In config.ini: [sorting] \n new_app_second=true
    bool getNewAppSortSecond() {
        return get("sorting/new_app_second", false).toBool();
    }

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

    /// 切换器显示期间轮询新窗口并动态入列。
    /// In config.ini: [dynamic] \n watch_new_windows=true
    bool getWatchNewWindows() {
        return get("dynamic/watch_new_windows", false).toBool();
    }

    /// 新窗口入列时自动把选中项跳过去。`watch_new_windows` 关闭时此项无效果。
    /// In config.ini: [dynamic] \n auto_select_new=true
    bool getAutoSelectNew() {
        return get("dynamic/auto_select_new", false).toBool();
    }

private:
    mutable QStringList blacklistCache_;
    mutable bool blacklistCacheValid_ = false;

    explicit ConfigManager(const QString& filename) : ConfigManagerBase(filename) {
        // 配置文件经托盘 Settings 编辑保存后失效缓存
        connect(this, &ConfigManagerBase::configEdited, this, [this] {
            blacklistCacheValid_ = false;
        });
    }
};


#endif //WIN_SWITCHER_CONFIGMANAGER_H
