#ifndef WIN_SWITCHER_GROUPWINDOWLIST_H
#define WIN_SWITCHER_GROUPWINDOWLIST_H

#include <QListWidget>
#include <QList>
#include <QSize>
#include <Windows.h>
#include "utils/WindowInfo.h"

/// Alt+` 的竖排窗口列表：只管展示与选中，不管排序。
/// 调用方负责把窗口按最后使用时间降序排好再传进来。
class GroupWindowList {
public:
    static constexpr int RowHeight = 28;      // 每行高度
    static constexpr int MinWidth = 220;      // 宽度下限
    static constexpr int MaxWidth = 480;      // 宽度上限
    static constexpr int MaxVisibleRows = 8;  // 超过则内部滚动
    static constexpr int PaddingH = 24;       // 水平内边距（左右各 12）
    static constexpr int PaddingV = 16;       // 垂直内边距

    explicit GroupWindowList(QWidget* parent);

    QListWidget* widget() { return list_; }
    void fill(const QList<WindowInfo>& windows);
    void clear();
    int count() const;
    int currentRow() const;
    void setCurrentRow(int row);
    void moveSelection(int delta);
    HWND currentHwnd() const;
    int append(const WindowInfo& info);
    int removeByHwnd(HWND hwnd);
    QSize preferredSize() const;

private:
    /// 按当前内容重算宽度并应用到列表与每一行（fill / append / removeByHwnd 后都要调）
    void applyPreferredWidth();
    QListWidget* list_ = nullptr;
};

#endif //WIN_SWITCHER_GROUPWINDOWLIST_H
