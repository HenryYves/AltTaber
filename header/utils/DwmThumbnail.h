#ifndef DWMTHUMBNAIL_H
#define DWMTHUMBNAIL_H

#include <windows.h>
#include <dwmapi.h>

/// DWM 缩略图封装：把一个源窗口的实时内容绘制到目标顶层窗口的指定矩形内。
/// 目标窗口通常是组列表旁的独立缩略图宿主。坐标与尺寸一律为物理像素。
class DwmThumbnail {
public:
    explicit DwmThumbnail(HWND target);
    ~DwmThumbnail();

    DwmThumbnail(const DwmThumbnail&) = delete;
    DwmThumbnail& operator=(const DwmThumbnail&) = delete;

    /// 切换缩略图源：先注销旧源，再注册新源。
    /// 源为空、等于目标窗口或注册失败时只隐藏，返回 false。
    /// 传入与当前已注册源相同的 hwnd 时保持不变（下游合并定时器会重复调用）。
    bool setSource(HWND source);

    /// 设置目标矩形（物理像素）并显示缩略图。未注册源时仅记录，待注册后生效。
    void setDestinationRect(const RECT& rect);

    /// 只关闭可见性，保留注册；之后 setDestinationRect() 可重新显示。
    void hide();

private:
    void unregister();

    HWND target_;
    HTHUMBNAIL thumbnail_ = nullptr;
    HWND source_ = nullptr;
};

#endif // DWMTHUMBNAIL_H
