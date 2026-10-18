#ifndef WIN_SWITCHER_WINDOWDIFFTRACKER_H
#define WIN_SWITCHER_WINDOWDIFFTRACKER_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <set>
#include <vector>

namespace Util {

/// 一轮窗口集合的差异。added/removed 均按句柄值升序。
struct WindowDiff {
    std::vector<HWND> added;
    std::vector<HWND> removed;
    bool empty() const { return added.empty() && removed.empty(); }
};

/// 记住"上一轮见过哪些窗口"，用来在轮询时找出新出现 / 已消失的窗口。
/// 纯数据 + 纯 STL，不依赖 Qt、不调用任何 Win32 API —— 这是本改动里
/// 唯一能脱离 GUI 做单元测试的部分。
class WindowDiffTracker {
public:
    /// 与上一次的集合做差异，然后把 known_ 更新为 current
    WindowDiff update(const std::vector<HWND>& current);

    /// 只记录，不产生差异。用于"开始轮询时先把现有窗口吃进去"，
    /// 否则第一轮会把所有已有窗口都报成新增。
    void seed(const std::vector<HWND>& current);

    void reset();
    size_t knownCount() const { return known_.size(); }

private:
    std::set<HWND> known_;
};

inline WindowDiff WindowDiffTracker::update(const std::vector<HWND>& current) {
    // std::set 迭代天然按句柄值升序，保证输出的确定性
    std::set<HWND> next(current.begin(), current.end());
    WindowDiff diff;
    for (auto hwnd: next)
        if (known_.find(hwnd) == known_.end())
            diff.added.push_back(hwnd);
    for (auto hwnd: known_)
        if (next.find(hwnd) == next.end())
            diff.removed.push_back(hwnd);
    known_ = std::move(next);
    return diff;
}

inline void WindowDiffTracker::seed(const std::vector<HWND>& current) {
    known_ = std::set<HWND>(current.begin(), current.end());
}

inline void WindowDiffTracker::reset() {
    known_.clear();
}

} // namespace Util

#endif //WIN_SWITCHER_WINDOWDIFFTRACKER_H
