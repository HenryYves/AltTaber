#ifndef WIN_SWITCHER_WINDOWACTIVATIONORDER_H
#define WIN_SWITCHER_WINDOWACTIVATIONORDER_H

#include <QString>
#include <QHash>
#include <QList>
#include <QDateTime>
#include <QPair>
#include <Windows.h>

/// 记录"每个进程的各个窗口最近一次成为前台的时间"，并提供按此排序的查询。
/// 这些逻辑原本是 Widget 的私有成员与私有方法，但 Alt+` 组列表、任务栏滚轮、
/// app 列表排序都要用，所以抽成独立单元。
class WindowActivationOrder {
public:
    /// 记录一次前台变化
    void record(const QString& exePath, HWND hwnd, const QDateTime& when);

    /// 该进程中最近活跃的窗口（不保证仍然有效）
    QPair<HWND, QDateTime> lastActive(const QString& exePath) const;

    /// windows 里最近活跃、且确实有记录的那个；没有则返回 {nullptr, QDateTime()}
    QPair<HWND, QDateTime> lastValidActive(const QString& exePath, const QList<HWND>& windows) const;

    /// 按活跃时间倒序重排 windows；该进程没有任何记录时**保持原顺序不动**
    void sortByActiveOrder(QList<HWND>& windows, const QString& exePath) const;

    /// 该进程当前所有有效窗口，按活跃时间倒序
    QList<HWND> orderedWindowsOf(const QString& exePath) const;

    void clear();

private:
    /// exePath -> (HWND, time)
    QHash<QString, QHash<HWND, QDateTime>> order_;
};

#endif //WIN_SWITCHER_WINDOWACTIVATIONORDER_H
