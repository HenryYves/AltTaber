#ifndef WIN_SWITCHER_WINDOWWATCHER_H
#define WIN_SWITCHER_WINDOWWATCHER_H

#include <QObject>
#include <QTimer>
#include <QList>
#include <Windows.h>
#include "utils/WindowDiffTracker.h"

/// 列表显示期间轮询窗口集合，把"新出现/已消失"的窗口报出去。
/// 只在列表可见时运行，不常驻后台。
class WindowWatcher : public QObject {
    Q_OBJECT

public:
    /// 300ms。依据：Util::listValidWindows() 自注释约 2ms（Util.cpp:323），
    /// 占空比 < 1%；且远低于"注意到新窗口出现"的感知阈值。
    static constexpr int IntervalMs = 300;

    explicit WindowWatcher(QObject* parent = nullptr);

    /// 开始轮询。先用当前窗口集合播种，因此第一轮不会把已有窗口报成"新增"
    void start();
    /// 停止轮询并清空已知集合
    void stop();

signals:
    void newWindowsFound(const QList<HWND>& added);
    void windowsGone(const QList<HWND>& removed);

private:
    QTimer timer_;
    Util::WindowDiffTracker tracker_;
};

#endif //WIN_SWITCHER_WINDOWWATCHER_H
