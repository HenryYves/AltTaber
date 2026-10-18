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
    static constexpr int DefaultRowHeight = 28; // 默认字体下的行高（也是下限，即既有观感）
    static constexpr int MinWidth = 220;      // 宽度下限（不随字号变：它只是个地板）
    static constexpr int MaxWidth = 480;      // 宽度上限（**默认字体下**的值，随字号等比放宽）
    static constexpr int MaxVisibleRows = 8;  // 超过则内部滚动
    static constexpr int PaddingH = 24;       // 水平内边距（左右各 12）
    static constexpr int PaddingV = 16;       // 垂直内边距

    explicit GroupWindowList(QWidget* parent);

    QListWidget* widget() { return list_; }

    /// 应用字号/字体，并按新字体的度量重算行高与宽度上限。
    /// 行高 = max(DefaultRowHeight, 字体 height + 8)：默认字体（Microsoft YaHei UI 9pt,
    /// height=15）下结果是 28，即不配置时与改动前逐像素一致（真机实测见
    /// docs/2026-10-07-group-list-font-design.md §3.2）。字号变大而不同步行高会裁字。
    void applyFont(const QFont& font);

    /// 宽度上限的屏幕兜底：字号放大时上限等比放宽，但不能超出屏幕可用宽度，
    /// 否则居中摆放会被裁在屏幕两侧（layoutForCurrentMode() 只居中，不兜宽度）
    void setWidthCap(int px);

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
    /// 按当前字号算出的宽度上限：MaxWidth × 字高比，再被屏幕兜底钳一次
    int effectiveMaxWidth() const;

    QListWidget* list_ = nullptr;
    int rowHeight_ = DefaultRowHeight;
    /// 相对「应用默认字体」的宽度缩放比。默认字体下必须是 1.0，这样不配置时宽度上限仍是 480
    double widthScale_ = 1.0;
    /// 屏幕可用宽度上限，0 = 不设（由 Widget 在布局前用 setWidthCap() 设）
    int widthCap_ = 0;
};

#endif //WIN_SWITCHER_GROUPWINDOWLIST_H
