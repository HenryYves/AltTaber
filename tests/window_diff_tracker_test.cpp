// 纯 C++ 测试，不依赖 Qt / 不依赖任何测试框架。
// 运行后在 stdout 打印每条断言的结果，全部通过时返回 0，否则返回 1。
#include "utils/WindowDiffTracker.h"
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

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

static HWND h(intptr_t v) { return reinterpret_cast<HWND>(v); }

static std::vector<HWND> vec(std::initializer_list<intptr_t> vs) {
    std::vector<HWND> out;
    for (auto v: vs) out.push_back(h(v));
    return out;
}

static int main_impl() {
    using Util::WindowDiffTracker;

    // 1. 空 tracker 的第一次 update：全部算新增，没有消失
    {
        WindowDiffTracker t;
        auto d = t.update(vec({1, 2, 3}));
        check(d.added == vec({1, 2, 3}), "empty tracker: all current windows are added");
        check(d.removed.empty(), "empty tracker: nothing removed");
        check(t.knownCount() == 3, "known count becomes 3");
    }

    // 2. 集合完全不变 → 无差异
    {
        WindowDiffTracker t;
        t.update(vec({1, 2, 3}));
        auto d = t.update(vec({1, 2, 3}));
        check(d.empty(), "unchanged set produces an empty diff");
    }

    // 3. 只增不删
    {
        WindowDiffTracker t;
        t.update(vec({1, 2}));
        auto d = t.update(vec({1, 2, 3}));
        check(d.added == vec({3}), "one new window is reported as added");
        check(d.removed.empty(), "no removals when a window appears");
    }

    // 4. 只删不增
    {
        WindowDiffTracker t;
        t.update(vec({1, 2, 3}));
        auto d = t.update(vec({1, 3}));
        check(d.added.empty(), "no additions when a window disappears");
        check(d.removed == vec({2}), "one closed window is reported as removed");
    }

    // 5. 同时增删
    {
        WindowDiffTracker t;
        t.update(vec({1, 2}));
        auto d = t.update(vec({2, 9}));
        check(d.added == vec({9}), "simultaneous: correct addition");
        check(d.removed == vec({1}), "simultaneous: correct removal");
    }

    // 6. 输出与输入顺序无关（两个方向都试）
    {
        WindowDiffTracker t;
        t.update(vec({1, 2, 3}));
        auto a = t.update(vec({7, 5}));  // 降序传入
        check(a.added == vec({5, 7}), "added is sorted ascending regardless of input order");
        check(a.removed == vec({1, 2, 3}), "removed is sorted ascending");
    }

    // 7. seed 之后不报新增（这是"列表刚弹出时不该入列任何东西"的防线）
    {
        WindowDiffTracker t;
        t.seed(vec({1, 2, 3}));
        auto d = t.update(vec({1, 2, 3}));
        check(d.empty(), "seed() suppresses the first-round false additions");
        check(t.knownCount() == 3, "seed() records the windows");
    }

    // 8. reset 之后回到"全部算新增"
    {
        WindowDiffTracker t;
        t.update(vec({1, 2, 3}));
        t.reset();
        check(t.knownCount() == 0, "reset clears known set");
        auto d = t.update(vec({1}));
        check(d.added == vec({1}), "after reset the window counts as added again");
    }

    // 9. 输入里的重复句柄只算一个
    {
        WindowDiffTracker t;
        auto d = t.update(vec({4, 4, 5}));
        check(d.added == vec({4, 5}), "duplicate handles collapse to one");
        check(t.knownCount() == 2, "duplicate handles counted once");
    }

    return g_failed == 0 ? 0 : 1;
}

int main() {
    const int rc = main_impl();
    std::printf("\n%d/%d passed\n", g_total - g_failed, g_total);
    return rc;
}
