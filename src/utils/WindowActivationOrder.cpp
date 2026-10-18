#include "utils/WindowActivationOrder.h"
#include "utils/Util.h"
#include <algorithm>

void WindowActivationOrder::record(const QString& exePath, HWND hwnd, const QDateTime& when) {
    order_[exePath].insert(hwnd, when);
}

QPair<HWND, QDateTime> WindowActivationOrder::lastActive(const QString& exePath) const {
    auto hwndOrder = order_.value(exePath);
    if (hwndOrder.isEmpty()) return {nullptr, QDateTime()};
    // QHash & QMap deref to value(QDateTime) rather than QPair
    auto iter = std::max_element(hwndOrder.begin(), hwndOrder.end());
    return {iter.key(), iter.value()};
}

QPair<HWND, QDateTime> WindowActivationOrder::lastValidActive(const QString& exePath,
                                                              const QList<HWND>& windows) const {
    auto hwndOrder = order_.value(exePath);
    if (hwndOrder.isEmpty()) return {nullptr, QDateTime()};

    QList<HWND> ordered = windows;
    sortByActiveOrder(ordered, exePath);

    if (auto time = hwndOrder.value(ordered.first()); !time.isNull())
        return {ordered.first(), time};
    else // check if the first HWND is recorded
        return {nullptr, QDateTime()};
}

/// sort Windows of [Group specified by exePath], by active order (latest first)
void WindowActivationOrder::sortByActiveOrder(QList<HWND>& windows, const QString& exePath) const {
    auto activeOrdMap = order_.value(exePath);
    if (activeOrdMap.isEmpty()) return;
    // sort by active order
    std::sort(windows.begin(), windows.end(), [&activeOrdMap](HWND a, HWND b) {
        return activeOrdMap.value(a) > activeOrdMap.value(b); // default value if not found
    }); // TODO update winActiveOrder! (remove invalid HWND)
}

/// group by exePath, sort by active order (last active first)
QList<HWND> WindowActivationOrder::orderedWindowsOf(const QString& exePath) const {
    auto windows = Util::listValidWindows(exePath); // filter by path
    sortByActiveOrder(windows, exePath);
    return windows;
}
