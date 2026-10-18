#include "utils/WindowWatcher.h"
#include "utils/Util.h"

namespace {
    /// QList<HWND> -> std::vector<HWND>
    /// 不用 QList::toVector()，避免 Qt 版本间 API 可用性差异
    std::vector<HWND> toVector(const QList<HWND>& list) {
        std::vector<HWND> out;
        out.reserve(size_t(list.size()));
        for (auto hwnd: list) out.push_back(hwnd);
        return out;
    }
}

WindowWatcher::WindowWatcher(QObject* parent) : QObject(parent) {
    timer_.setInterval(IntervalMs);
    connect(&timer_, &QTimer::timeout, this, [this] {
        auto diff = tracker_.update(toVector(Util::listValidWindows()));
        if (!diff.added.empty())
            emit newWindowsFound(QList<HWND>(diff.added.begin(), diff.added.end()));
        if (!diff.removed.empty())
            emit windowsGone(QList<HWND>(diff.removed.begin(), diff.removed.end()));
    });
}

void WindowWatcher::start() {
    tracker_.seed(toVector(Util::listValidWindows())); // 播种，见头文件契约
    if (!timer_.isActive()) timer_.start();
}

void WindowWatcher::stop() {
    timer_.stop();
    tracker_.reset();
}
