#include "utils/GroupWindowList.h"
#include <QApplication>
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
    // ! 基础态那句 `background-color: transparent` 不是多余的：Qt 只在这个选择器命中
    // ! 且真的画了盒子时才会套用 `padding-*`。少了它，未选中行不缩进（文字 x=3）、
    // ! 选中行却缩进一次（x=15），看起来就是"选中项往右跳 12px"。
    // ! 离屏探针实测：加上后两态文字 x 都是 15、高亮仍是满宽（见 HANDOFF 2026-10-07 一节）。
    list_->setStyleSheet(R"(
        QListWidget {
            background-color: transparent;
            border: none;
            outline: none;
        }
        QListWidget::item {
            color: rgb(240, 240, 240);
            background-color: transparent;
            padding-left: 12px;
            padding-right: 12px;
        }
        QListWidget::item:selected {
            background-color: rgba(255, 255, 255, 38);
            border-radius: 4px;
        }
    )");
    list_->hide();
    // 用继承来的字体初始化行高/宽度缩放。此时它是应用默认字体，所以 rowHeight_ 仍是 28、
    // widthScale_ 仍是 1.0 —— 不配置 [group_list] 时一切都与改动前一致
    applyFont(list_->font());
}

void GroupWindowList::applyFont(const QFont& font) {
    list_->setFont(font);
    const QFontMetrics fm(font);
    // 行高随字号走。下限 28 = 默认字体下的高度，所以不配置时行高不变（真机实测见头文件注释）
    rowHeight_ = qMax(DefaultRowHeight, fm.height() + 8);
    // 宽度上限按「相对应用默认字体的字高比」等比放宽。基准取 QApplication::font() 而不是写死
    // 15px，是为了在 100%/125%/150% DPI 下都自动成立：默认字体下比值恒为 1.0 → 上限仍是 480
    const int baseHeight = QFontMetrics(QApplication::font()).height();
    widthScale_ = baseHeight > 0 ? double(fm.height()) / double(baseHeight) : 1.0;
    applyPreferredWidth();
}

void GroupWindowList::setWidthCap(int px) {
    widthCap_ = px;
    // 行的 sizeHint 也要一起更新：只改视图尺寸会让高亮条比视图窄
    applyPreferredWidth();
}

int GroupWindowList::effectiveMaxWidth() const {
    int w = int(MaxWidth * widthScale_ + 0.5);
    if (widthCap_ > 0) w = qMin(w, widthCap_);
    return qMax(MinWidth, w);   // 屏幕兜底再狠也不能把上限压到下限之下
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
    item->setSizeHint(QSize(MinWidth, rowHeight_));
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
    // 宽度按最长标题算，clamp 到 [MinWidth, 随字号放宽后的上限]
    int textWidth = 0;
    for (int i = 0; i < list_->count(); ++i)
        textWidth = qMax(textWidth, list_->fontMetrics().horizontalAdvance(list_->item(i)->text()));
    const int width = qBound(MinWidth, textWidth + PaddingH, effectiveMaxWidth());
    const int rows = qBound(1, list_->count(), MaxVisibleRows);
    return {width, rows * rowHeight_ + PaddingV};
}

void GroupWindowList::applyPreferredWidth() {
    const int width = preferredSize().width();
    for (int i = 0; i < list_->count(); ++i)
        list_->item(i)->setSizeHint(QSize(width, rowHeight_));
    list_->setFixedWidth(width);
}

void GroupWindowList::fill(const QList<WindowInfo>& windows) {
    list_->clear();
    for (auto& info: windows) {
        auto* item = new QListWidgetItem(info.title);
        item->setData(Qt::UserRole, QVariant::fromValue(info));
        item->setSizeHint(QSize(MinWidth, rowHeight_));   // 宽度先占位，下面统一重设
        list_->addItem(item);
    }
    applyPreferredWidth();
}
