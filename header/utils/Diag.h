#ifndef WIN_SWITCHER_DIAG_H
#define WIN_SWITCHER_DIAG_H

#include <windows.h>
#include <QByteArray>
#include <QDebug>
#include <QString>
#include "utils/Util.h"

/// 诊断日志：设置环境变量 ALTTABER_DIAG=1（也接受 true/yes/on，不区分大小写）后启用。
/// 每行以固定标记开头，便于 grep：
///   #FG       前台窗口变化（main.cpp 的 WinEvent 回调 / Widget::notifyForegroundChanged）
///   #SWITCH   Util::switchToWindow 的调用点
///   #TB_ENTER 进入任务栏
///   #TB_LEAVE 离开任务栏
///   #THUMB    任务栏缩略图窗口（TaskListThumbnailWnd）可见性变化
/// 固定字段：hwnd、类名、标题、光标坐标、光标下窗口类名、缩略图窗口是否可见、本程序是否可见。
/// 关闭诊断时不取窗口信息、不打印，开销只有一次静态初始化。
namespace Diag {

    /// ALTTABER_DIAG 只读取一次
    inline bool enabled() {
        static const bool on = [] {
            const QByteArray value = qgetenv("ALTTABER_DIAG").trimmed().toLower();
            return value == "1" || value == "true" || value == "yes" || value == "on";
        }();
        return on;
    }

    /// 本程序主窗口（Widget）句柄，由 Widget 构造时登记，供 selfVisible 字段使用
    inline HWND& selfHwnd() {
        static HWND hwnd = nullptr;
        return hwnd;
    }

    inline void setSelfHwnd(HWND hwnd) {
        selfHwnd() = hwnd;
    }

    inline bool selfVisible() {
        return IsWindowVisible(selfHwnd()) != FALSE;
    }

    /// 标题里可能有引号/换行，压成单行，保证一条诊断只占一行
    inline QString oneLine(QString text) {
        text.replace(QLatin1Char('"'), QLatin1Char('\''));
        text.replace(QLatin1Char('\r'), QLatin1Char(' '));
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        return text;
    }

    /// 打印一条诊断。hwnd 为本次事件的主角窗口；其他字段按当前桌面状态现场采集
    inline void log(const char* marker, HWND hwnd) {
        if (!enabled()) return;

        POINT cursor{};
        GetCursorPos(&cursor); // 物理像素
        const HWND underCursor = Util::topWindowFromPoint(cursor);
        const HWND thumbnail = Util::getCurrentTaskListThumbnailWnd();

        const QString line =
                QStringLiteral("%1 hwnd=0x%2 class=\"%3\" title=\"%4\" cursor=%5,%6 cursorClass=\"%7\" "
                               "thumbVisible=%8 selfVisible=%9")
                        .arg(QString::fromLatin1(marker))
                        .arg(static_cast<qulonglong>(reinterpret_cast<quintptr>(hwnd)), 0, 16)
                        .arg(oneLine(Util::getClassName(hwnd)))
                        .arg(oneLine(Util::getWindowTitle(hwnd)))
                        .arg(cursor.x)
                        .arg(cursor.y)
                        .arg(oneLine(Util::getClassName(underCursor)))
                        .arg(IsWindowVisible(thumbnail) ? 1 : 0)
                        .arg(selfVisible() ? 1 : 0);
        qDebug().noquote() << line;
    }

} // Diag

#endif //WIN_SWITCHER_DIAG_H
