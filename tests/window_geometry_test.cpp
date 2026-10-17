// 纯 C++ 测试，不依赖 Qt / 不依赖任何测试框架。
// 运行后在 stdout 打印每条断言的结果，全部通过时返回 0，否则返回 1。
#include "utils/WindowGeometry.h"
#include <cstdio>
#include <string>

static int g_total = 0;
static int g_failed = 0;

static void check(bool cond, const std::string& what) {
    ++g_total;
    if (cond) {
        std::printf("ok   %s\n", what.c_str());
    } else {
        ++g_failed;
        std::printf("FAIL %s\n", what.c_str());
    }
}

using WindowGeometry::Rect;

static bool same(const Rect& r, int x, int y, int w, int h) {
    return r.x == x && r.y == y && r.width == w && r.height == h;
}

static std::string fmt(const Rect& r) {
    return "(" + std::to_string(r.x) + "," + std::to_string(r.y) + "," +
           std::to_string(r.width) + "x" + std::to_string(r.height) + ")";
}

// 断言矩形相等，失败时把实际值打出来，方便夜间无人值守时定位
static void checkRect(const Rect& actual, int x, int y, int w, int h, const std::string& what) {
    if (same(actual, x, y, w, h)) {
        check(true, what);
    } else {
        check(false, what + "  got " + fmt(actual));
    }
}

static int main_impl() {
    using WindowGeometry::centerInWorkArea;
    using WindowGeometry::fitAspect;
    using WindowGeometry::thumbnailRect;

    // ---- centerInWorkArea ----
    // 1. 主屏 1920x1080 居中
    checkRect(centerInWorkArea(400, 300, Rect{0, 0, 1920, 1080}),
              760, 390, 400, 300, "center: even margins on primary work area");
    // 2. 副屏带原点偏移时仍相对工作区居中
    checkRect(centerInWorkArea(400, 300, Rect{1920, 0, 1920, 1080}),
              2680, 390, 400, 300, "center: respects work area origin offset");
    // 3. 奇数差值向下取整
    checkRect(centerInWorkArea(401, 301, Rect{0, 0, 1920, 1080}),
              759, 389, 401, 301, "center: odd margins floor");
    // 4. 比工作区宽 → 贴左，垂直仍居中
    checkRect(centerInWorkArea(2200, 900, Rect{0, 0, 1920, 1080}),
              0, 90, 2200, 900, "center: wider than work area pins x to left");
    // 5. 比工作区高 → 贴上，水平仍居中
    checkRect(centerInWorkArea(400, 1200, Rect{0, 0, 1920, 1080}),
              760, 0, 400, 1200, "center: taller than work area pins y to top");
    // 6. 两个方向都超出 → 贴左上角
    checkRect(centerInWorkArea(2200, 1200, Rect{0, 0, 1920, 1080}),
              0, 0, 2200, 1200, "center: oversized both ways pins to top-left");
    // 7. 带偏移的工作区里的居中
    checkRect(centerInWorkArea(200, 100, Rect{100, 50, 800, 600}),
              400, 300, 200, 100, "center: offset work area centers correctly");
    // 8. 0 尺寸窗口落在工作区中心点
    checkRect(centerInWorkArea(0, 0, Rect{0, 0, 1920, 1080}),
              960, 540, 0, 0, "center: zero-size window lands on center point");

    // ---- fitAspect ----
    // 9. 同比例恰好填满
    checkRect(fitAspect(1920, 1080, 320, 180), 0, 0, 320, 180,
              "fitAspect: same aspect fills the box exactly");
    // 10. 宽度受限
    checkRect(fitAspect(1600, 900, 320, 200), 0, 0, 320, 180,
              "fitAspect: width-limited keeps aspect");
    // 11. 高度受限
    checkRect(fitAspect(800, 1000, 400, 300), 0, 0, 240, 300,
              "fitAspect: height-limited keeps aspect");
    // 12. 允许放大（源比上限盒小）
    checkRect(fitAspect(100, 50, 400, 400), 0, 0, 400, 200,
              "fitAspect: upscales small sources");
    // 13. 源宽非法 → 返回上限盒
    checkRect(fitAspect(0, 100, 400, 300), 0, 0, 400, 300,
              "fitAspect: zero source width returns the limit box");
    // 14. 源高非法 → 返回上限盒
    checkRect(fitAspect(100, 0, 400, 300), 0, 0, 400, 300,
              "fitAspect: zero source height returns the limit box");
    // 15. 负尺寸同样视为非法
    checkRect(fitAspect(-10, 50, 400, 300), 0, 0, 400, 300,
              "fitAspect: negative source dims return the limit box");
    // 16. 取整向下、不超出上限盒（高度受限：999 * 333 / 1000 = 332）
    checkRect(fitAspect(999, 1000, 333, 333), 0, 0, 332, 333,
              "fitAspect: rounding down never exceeds the box");
    // 17. 上限盒为 0 → 0x0
    checkRect(fitAspect(10, 10, 0, 100), 0, 0, 0, 0,
              "fitAspect: zero-width limit box collapses");
    // 18. 极端细长源
    checkRect(fitAspect(16, 9, 32, 18), 0, 0, 32, 18,
              "fitAspect: exact 16:9 at small scale");

    // ---- thumbnailRect ----
    // 19. 列表右侧 + 相对列表垂直居中
    checkRect(thumbnailRect(Rect{100, 100, 400, 300}, 320, 180, 12, 1000, 600),
              512, 160, 320, 180, "thumbnail: right of list, vertically centered");
    // 20. 超出客户区右边界时向左钳位
    checkRect(thumbnailRect(Rect{600, 100, 300, 300}, 320, 180, 12, 1200, 600),
              880, 160, 320, 180, "thumbnail: clamps to right client edge");
    // 21. 底部越界时向上钳位
    checkRect(thumbnailRect(Rect{100, 500, 400, 40}, 320, 180, 12, 1200, 600),
              512, 420, 320, 180, "thumbnail: clamps to bottom client edge");
    // 22. 间距为 0 时紧贴列表右缘
    checkRect(thumbnailRect(Rect{100, 0, 400, 400}, 320, 180, 0, 1000, 600),
              500, 110, 320, 180, "thumbnail: zero gap hugs the list");
    // 23. 缩略图比客户区还大 → 贴左上角
    checkRect(thumbnailRect(Rect{100, 100, 400, 300}, 1300, 700, 12, 1200, 600),
              0, 0, 1300, 700, "thumbnail: oversized thumbnail pins to top-left");
    // 24. 列表已占满客户区宽 → 缩略图被压回客户区内
    checkRect(thumbnailRect(Rect{0, 0, 800, 600}, 320, 180, 12, 800, 600),
              480, 210, 320, 180, "thumbnail: full-width list still keeps thumbnail inside");

    return g_failed == 0 ? 0 : 1;
}

int main() {
    const int rc = main_impl();
    std::printf("\n%d/%d passed\n", g_total - g_failed, g_total);
    return rc;
}
