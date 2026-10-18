#ifndef WIN_SWITCHER_WINDOWGEOMETRY_H
#define WIN_SWITCHER_WINDOWGEOMETRY_H

#include <algorithm>
#include <cstdint>

/// 纯几何计算：不依赖 Qt、不依赖 Win32，只用普通整数结构。
/// 坐标一律是「同一坐标系下的整数像素」，调用方负责把逻辑坐标 / 物理像素
/// 换算好再传进来（见 f3：Win32 矩形与 DWM 缩略图坐标都是物理像素）。
namespace WindowGeometry {

/// 左上角原点 + 宽高的矩形。宽高为 0 表示退化矩形，负值一律按 0 处理。
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/// 把 w×h 的窗口居中放进工作区 work，并钳位，保证整个窗口落在工作区内。
/// 窗口比工作区还大时贴左上角（此时无法完整放下，只能保证左上对齐）。
inline Rect centerInWorkArea(int w, int h, const Rect& work) {
    const int cw = std::max(0, w);
    const int ch = std::max(0, h);
    const int ww = std::max(0, work.width);
    const int wh = std::max(0, work.height);

    int x;
    int y;
    if (cw > ww) {
        x = work.x; // 比工作区宽：贴左
    } else {
        x = work.x + (ww - cw) / 2;
        x = std::clamp(x, work.x, work.x + (ww - cw));
    }
    if (ch > wh) {
        y = work.y; // 比工作区高：贴上
    } else {
        y = work.y + (wh - ch) / 2;
        y = std::clamp(y, work.y, work.y + (wh - ch));
    }
    return Rect{x, y, cw, ch};
}

/// 把 srcW×srcH 等比缩放到 maxW×maxH 以内，返回缩放后的尺寸（原点为 0,0）。
/// 源尺寸非法（<=0）时返回上限盒；上限盒为 0 时返回 0×0。
/// 只用整数运算，结果不会超出上限盒（向下取整）。
inline Rect fitAspect(int srcW, int srcH, int maxW, int maxH) {
    const int mw = std::max(0, maxW);
    const int mh = std::max(0, maxH);
    if (srcW <= 0 || srcH <= 0)
        return Rect{0, 0, mw, mh};
    if (mw == 0 || mh == 0)
        return Rect{0, 0, 0, 0};

    // 64 位交叉相乘，避免 srcW * maxH 之类的乘法溢出
    const std::int64_t crossSrc = static_cast<std::int64_t>(srcW) * mh;
    const std::int64_t crossDst = static_cast<std::int64_t>(srcH) * mw;
    if (crossSrc >= crossDst) {
        // 宽度受限：撑满最大宽，高度按比例向下取整
        const int outH = static_cast<int>(static_cast<std::int64_t>(srcH) * mw / srcW);
        return Rect{0, 0, mw, outH};
    }
    // 高度受限：撑满最大高，宽度按比例向下取整
    const int outW = static_cast<int>(static_cast<std::int64_t>(srcW) * mh / srcH);
    return Rect{0, 0, outW, mh};
}

/// GroupMode 内容区的唯一几何来源：缩略图是否放得下、内容尺寸、列表在内容区里的偏移。
/// 输入都是逻辑像素；`availableContentWidth` 已扣掉弹窗左右 margin。
struct GroupContent {
    int contentWidth = 0;
    int contentHeight = 0;
    int listX = 0;
    int listY = 0;
    bool thumbnailActive = false;
};

inline GroupContent groupContent(int listWidth, int listHeight,
                                 int thumbBoxWidth, int thumbBoxHeight,
                                 int gap, int minListWidth,
                                 int availableContentWidth, bool thumbnailRequested) {
    const int lw = std::max(0, listWidth);
    const int lh = std::max(0, listHeight);
    const int bw = std::max(0, thumbBoxWidth);
    const int bh = std::max(0, thumbBoxHeight);
    const int g = std::max(0, gap);
    const int avail = std::max(0, availableContentWidth);
    const bool active = thumbnailRequested && (std::max(0, minListWidth) + g + bw <= avail);

    GroupContent out;
    out.thumbnailActive = active;
    out.contentWidth = active ? lw + g + bw : lw;
    out.contentHeight = active ? std::max(lh, bh) : lh;
    out.listX = 0;
    out.listY = (out.contentHeight - lh) / 2;
    return out;
}

/// 把 thumbW×thumbH 的缩略图放在 listRect 右侧（间隔 gap）、相对列表垂直居中，
/// 并钳位在 clientW×clientH 的客户区内。缩略图比客户区还大时贴左上角。
/// 返回矩形只包含缩略图本身的尺寸，不含列表。
/// listRect 的宽高为负时按 0 处理（与 Rect 的契约一致），否则返回值会被带偏。
inline Rect thumbnailRect(const Rect& listRect, int thumbW, int thumbH, int gap,
                          int clientW, int clientH) {
    const int tw = std::max(0, thumbW);
    const int th = std::max(0, thumbH);
    const int cw = std::max(0, clientW);
    const int ch = std::max(0, clientH);
    const int lw = std::max(0, listRect.width);
    const int lh = std::max(0, listRect.height);

    int x = listRect.x + lw + gap;
    int y = listRect.y + (lh - th) / 2;

    // 水平钳位：先处理「比客户区还宽」，否则夹到 [0, cw - tw]
    if (tw > cw)
        x = 0;
    else
        x = std::clamp(x, 0, cw - tw);

    if (th > ch)
        y = 0;
    else
        y = std::clamp(y, 0, ch - th);

    return Rect{x, y, tw, th};
}

} // namespace WindowGeometry

#endif //WIN_SWITCHER_WINDOWGEOMETRY_H
