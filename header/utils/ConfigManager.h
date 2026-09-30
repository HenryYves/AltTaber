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
