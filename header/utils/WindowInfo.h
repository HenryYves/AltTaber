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
