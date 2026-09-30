#include "utils/GroupWindowList.h"
#include <QFontMetrics>

GroupWindowList::GroupWindowList(QWidget* parent) {
    list_ = new QListWidget(parent);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setFocusPolicy(Qt::NoFocus);           // 按键由 Widget 统一处理，列表不抢焦点
    list_->setUniformItemSizes(true);
    list_->setTextElideMode(Qt::ElideRight);      // 过长标题右省略
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setStyleSheet(R"(
        QListWidget {
            background-color: transparent;
            border: none;
            outline: none;
        }
        QListWidget::item {
            color: rgb(240, 240, 240);
            padding-left: 12px;
            padding-right: 12px;
        }
        QListWidget::item:selected {
            background-color: rgba(255, 255, 255, 38);
            border-radius: 4px;
        }
    )");
    list_->hide();
}

void GroupWindowList::clear() {
    list_->clear();
}

int GroupWindowList::count() const {
    return list_->count();
}

int GroupWindowList::currentRow() const {
    return list_->currentRow();
}

void GroupWindowList::setCurrentRow(int row) {
    if (list_->count() == 0) return;
    list_->setCurrentRow(qBound(0, row, list_->count() - 1));
}

void GroupWindowList::moveSelection(int delta) {
    const int n = list_->count();
    if (n == 0) return;
    // 端点循环
    list_->setCurrentRow(((list_->currentRow() + delta) % n + n) % n);
}

HWND GroupWindowList::currentHwnd() const {
    auto* item = list_->currentItem();
    if (!item) return nullptr;
    return item->data(Qt::UserRole).value<WindowInfo>().hwnd;
}

int GroupWindowList::append(const WindowInfo& info) {
    auto* item = new QListWidgetItem(info.title);
    item->setData(Qt::UserRole, QVariant::fromValue(info));
    item->setSizeHint(QSize(MinWidth, RowHeight));
    list_->addItem(item);
    applyPreferredWidth();   // 新行可能更长，宽度要跟着变
    return list_->count() - 1;
}

int GroupWindowList::removeByHwnd(HWND hwnd) {
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(Qt::UserRole).value<WindowInfo>().hwnd == hwnd) {
            delete list_->takeItem(i);
            applyPreferredWidth();
            return i;
        }
    }
    return -1;
}

QSize GroupWindowList::preferredSize() const {
    // 宽度按最长标题算，clamp 到 [MinWidth, MaxWidth]
    int textWidth = 0;
    for (int i = 0; i < list_->count(); ++i)
        textWidth = qMax(textWidth, list_->fontMetrics().horizontalAdvance(list_->item(i)->text()));
    const int width = qBound(MinWidth, textWidth + PaddingH, MaxWidth);
    const int rows = qBound(1, list_->count(), MaxVisibleRows);
    return {width, rows * RowHeight + PaddingV};
}

void GroupWindowList::applyPreferredWidth() {
    const int width = preferredSize().width();
    for (int i = 0; i < list_->count(); ++i)
        list_->item(i)->setSizeHint(QSize(width, RowHeight));
    list_->setFixedWidth(width);
}

void GroupWindowList::fill(const QList<WindowInfo>& windows) {
    list_->clear();
    for (auto& info: windows) {
        auto* item = new QListWidgetItem(info.title);
        item->setData(Qt::UserRole, QVariant::fromValue(info));
        item->setSizeHint(QSize(MinWidth, RowHeight));   // 宽度先占位，下面统一重设
        list_->addItem(item);
    }
    applyPreferredWidth();
}
