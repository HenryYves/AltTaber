#include "../header/widget.h"
#include "ui_Widget.h"
#include "utils/Util.h"
#include <QDebug>
#include <QWindow>
#include <QScreen>
#include "utils/setWindowBlur.h"
#include "utils/IconOnlyDelegate.h"
#include <QPainter>
#include <QPen>
#include <QDateTime>
#include "utils/QtWin.h"
#include <QWheelEvent>
#include <QTimer>
#include <QMetaEnum>
#include "utils/SystemTray.h"
#include "utils/ConfigManager.h"

/// 从 WindowGroup 里取出 hwnd 列表，顺序与 group.windows 一致
static QList<HWND> hwndsOfGroup(const WindowGroup& group) {
    QList<HWND> hwnds;
    for (auto& info: group.windows) hwnds << info.hwnd;
    return hwnds;
}

/// 设计文档 §2.3：GroupMode 下 Tab / 方向键 / Vim H J K L 都只是"上下移动高亮"。
/// 返回 0 表示该键在 GroupMode 里没有定义移动
static int groupNavDeltaOf(int key, Qt::KeyboardModifiers modifiers) {
    switch (key) {
        case Qt::Key_Tab:   return (modifiers & Qt::ShiftModifier) ? -1 : +1;
        case Qt::Key_Down:  // ↓
        case Qt::Key_Right: // →
        case Qt::Key_J:
        case Qt::Key_L:     return +1;
        case Qt::Key_Up:    // ↑
        case Qt::Key_Left:  // ←
        case Qt::Key_K:
        case Qt::Key_H:     return -1;
        default:            return 0;
    }
}

Widget::Widget(QWidget* parent) : QWidget(parent), ui(new Ui::Widget) {
    ui->setupUi(this);
    lw = ui->listWidget;
    setWindowFlag(Qt::WindowStaysOnTopHint);
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground); //设置窗口背景透明 !但是会造成show()时的闪烁 和 绘制延迟(?)
    QtWin::taskbarDeleteTab(this); //删除任务栏图标
    setWindowTitle("AltTaber");

    Util::setWindowRoundCorner(this->hWnd()); // 设置窗口圆角
    setWindowBlur(hWnd()); // 设置窗口模糊, 必须配合Qt::WA_TranslucentBackground

    setupLabelFont();

    lw->setViewMode(QListView::IconMode);
    lw->setMovement(QListView::Static);
    lw->setFlow(QListView::LeftToRight);
    lw->setWrapping(false);
    lw->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lw->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lw->setIconSize({64, 64});
    lw->setGridSize({80, 80});
    lw->setFixedHeight(lw->gridSize().height());
    lw->setUniformItemSizes(true); // optimization ?
    lw->setStyleSheet(R"(
        QListWidget {
            background-color: transparent;
            border: none;
            outline: none; /* 去除选中时的虚线框（在文字为空时，会形成闪电一样的标志 离谱） */
        }
    )");
    // 就算Text为Null，也会占用空间，很难做到真正的IConMode，所以只能delegate自绘
    // 本来为了去除图标选中变色样式，可以对Icon手动addPixmap(..., QIcon::Selected) or (& ~Qt::ItemIsSelectable)
    // 但是采用delegate后，就没必要了
    // will not take ownership of delegate
    lw->setItemDelegate(new IconOnlyDelegate(lw));
    lw->installEventFilter(this);

    connect(lw, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* cur, QListWidgetItem*) {
        if (cur) showLabelForItem(cur);
    });

    connect(qApp, &QApplication::focusWindowChanged, this, [this](QWindow* focusWindow) {
        if (focusWindow == nullptr) {
            if (!this->underMouse()) // hide when lost focus & mouse outside (means user choose to)
                hide();
            else { // Windows Terminal will do
                qWarning() << "Someone tried to steal focus!";
            }
        }
    });

    groupList_ = new GroupWindowList(this);
    groupList_->widget()->installEventFilter(this);   // 滚轮移动高亮（见 Step 2b）
}

Widget::~Widget() {
    delete ui;
}

void Widget::keyPressEvent(QKeyEvent* event) {
    auto key = event->key();
    auto modifiers = event->modifiers();
    static const QHash<int, int> VimArrows = {
        {Qt::Key_K, Qt::Key_Up},    // ↑
        {Qt::Key_J, Qt::Key_Down},  // ↓
        {Qt::Key_H, Qt::Key_Left},  // ←
        {Qt::Key_L, Qt::Key_Right}, // →
    };
    if (key == Qt::Key_Tab && mode_ == Mode::AppMode) { // switch to next or prev
        auto i = lw->currentRow();
        bool isShiftPressed = (modifiers & Qt::ShiftModifier);
        // weird formula, but works (hhh)
        auto index = (i - (2 * isShiftPressed - 1) + lw->count()) % lw->count();
        lw->setCurrentRow(index);
    } else if (key == Qt::Key_QuoteLeft && (modifiers & Qt::AltModifier)) {
        const int delta = (modifiers & Qt::ShiftModifier) ? -1 : 1;
        if (mode_ == Mode::GroupMode && this->isVisible()) {
            // 情形 C：已在组列表内 —— 移动高亮
            groupList_->moveSelection(delta);
            userNavigated_ = true;
        } else {
            // 情形 A（不可见）/ 情形 B（AppMode 可见，由 Task 8 接管）
            requestShowGroup();
        }
        return;
    } else if (mode_ == Mode::AppMode && (key == Qt::Key_Up || key == Qt::Key_Down)) {
        if (auto item = lw->currentItem()) {
            auto center = lw->visualItemRect(item).center();
            // 转发映射到WheelEvent
            auto wheelEvent = new QWheelEvent(center, lw->mapToGlobal(center), {},
                                              {key == Qt::Key_Up ? 120 : -120, 0},
                                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::postEvent(lw, wheelEvent);
        }
    } else if (mode_ == Mode::AppMode && (key == Qt::Key_Left || key == Qt::Key_Right)) { // 默认情况下 左右键可以切换item 只需要处理边界循环即可
        const int N = lw->count();
        const int i = lw->currentRow();
        if (key == Qt::Key_Left && i == 0)
            lw->setCurrentRow(N - 1);
        else if (key == Qt::Key_Right && i == N - 1)
            lw->setCurrentRow(0);
    } else if (mode_ == Mode::AppMode && VimArrows.contains(key)) { // map [K J H L] to [↑ ↓ ← →]
        QApplication::postEvent(lw, new QKeyEvent(QEvent::KeyPress, VimArrows.value(key), modifiers));
    } else if (mode_ == Mode::GroupMode) {
        // GroupMode 里所有剩下的键都归这一支：设计文档 §2.3 把 Tab / 方向键 / Vim H J K L
        // 定义成"移动高亮"。放在最后且不判 key，是为了保证**任何**键都不会落进上面那些分支——
        // 它们操作的是 `lw`（AppMode 的列表），在 GroupMode 下（列表隐藏但 currentItem 仍在）
        // 会转成 WheelEvent 打到真实窗口上（Task 7 复审 Important：↓ 会把窗口最小化）
        if (const int delta = groupNavDeltaOf(key, modifiers)) {
            groupList_->moveSelection(delta);
            userNavigated_ = true;
        }
    }
    QWidget::keyPressEvent(event);
}

bool Widget::forceShow() {
    setWindowOpacity(0.005); // 减少闪烁发生(因Qt::WA_TranslucentBackground) in showMinimized()
    showMinimized();
    showNormal();
    setWindowOpacity(1);
    return isForeground();
}

/// show App description under the icon
void Widget::showLabelForItem(QListWidgetItem* item, QString text) {
    if (!item) return;

    if (text.isNull()) {
        auto path = item->data(Qt::UserRole).value<WindowGroup>().exePath;
        text = Util::getFileDescription(path);
    }
    ui->label->setText(text);
    ui->label->adjustSize();

    auto itemRect = lw->visualItemRect(item);
    auto center = itemRect.center() + QPoint(0, itemRect.height() / 2 + ListWidgetMargin.bottom() / 2);
    center = lw->mapTo(this, center);
    auto labelRect = ui->label->rect();
    labelRect.moveCenter(center);

    auto bound = this->rect().marginsRemoved({5, 0, 5, 0});
    labelRect.moveRight(qMin(labelRect.right(), bound.right()));
    labelRect.moveLeft(qMax(labelRect.left(), bound.left())); // left align

    ui->label->move(labelRect.topLeft());
}

void Widget::setupLabelFont() {
    static auto reloadLabelFontCfg = [this] {
        const QStringList Fonts = {"Microsoft YaHei UI", "Microsoft YaHei", "Consolas"}; // fallback
        auto labelFont = ui->label->font();
        labelFont.setPointSize(cfg.get("label/font_size", 10).toInt());
        auto defaultFF = QStringList{cfg.get("label/font_family", Fonts[0]).toString()};
        labelFont.setFamilies(defaultFF << Fonts.mid(1));
        ui->label->setFont(labelFont);
        qDebug() << labelFont.families();
        qDebug() << "Label Actual Font:" << QFontInfo(labelFont).family();
    };
    reloadLabelFontCfg();

    // auto reload
    connect(&cfg, &ConfigManager::configEdited, this, [] {
        reloadLabelFontCfg();
    });
}

void Widget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Alt) {
        if (mode_ == Mode::GroupMode) {
            // 与 AppMode 的松手分支对称（那边也是 `if (this->isVisible())` 才提交）：只有列表确实在屏幕上时才提交。
            // 隐藏状态下提交等于"切到用户从没看见过的那一行"——例如中途点了别处触发 hide()，
            // 或上一次进入失败、mode_ 还留着 GroupMode 时又按了一次 Alt+`
            if (this->isVisible()) {
                commitGroupSelection();
                hide(); //! 与 AppMode 同理：必须在切换之后 hide，否则焦点会回落到上一个前台窗口
            }
            // 下面三行**故意放在门外**：无论有没有提交，Alt 抬起都必须把模式/列表收干净，
            // 不留一个隐藏着的 GroupMode 状态给下一次按键
            groupWindowOrder.clear();      // 与 AppMode 一致，Alt 抬起即清
            mode_ = Mode::AppMode;
            groupList_->clear();
            QWidget::keyReleaseEvent(event);
            return;
        }
        groupWindowOrder.clear(); // for Alt + `
        if (this->isVisible()) {
            // active selected window
            if (auto item = lw->currentItem()) {
                if (auto group = item->data(Qt::UserRole).value<WindowGroup>(); !group.windows.empty()) {
                    WindowInfo targetWin = group.windows.at(0); // TODO 需要排序（lastActiveWindow 被关闭情况下）
                    const auto lastActive = winActiveOrder_.lastActive(group.exePath).first;
                    for (auto& info: group.windows) {
                        if (info.hwnd == lastActive) {
                            targetWin = info;
                            break;
                        }
                    }
                    if (targetWin.hwnd) {
                        Util::switchToWindow(targetWin.hwnd);
                        qInfo() << "Switch to" << targetWin << group.exePath;
                    }
                }
            }
            hide(); //! must hide after active target window, or focus may fallback to prev foreground window (like 网易云音乐)
        }
    }
    QWidget::keyReleaseEvent(event);
}

void Widget::paintEvent(QPaintEvent*) { //不绘制会导致鼠标穿透背景
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen); //取消边框//pen决定边框颜色
    painter.setBrush(QColor(25, 25, 25, 100));
    painter.drawRect(rect());
}

/// 通知前台窗口变化
/// @param hwnd 前台窗口句柄
/// @param source 通知来源, for debug, @b Optional
void Widget::notifyForegroundChanged(HWND hwnd, ForegroundChangeSource source) { // TODO isVisible or AltDown时，关闭前台更新通知
    if (hwnd == this->hWnd()) return;
    // （其实监听前台变化只是为了排序，不需要太精确，可以放松限制）
    // 通过`EVENT_SYSTEM_FOREGROUND`触发时忽略`IsWindowVisible`，因为窗口在创建瞬间可能不可见
    if (!Util::isWindowAcceptable(hwnd, source == WinEvent)) return;
    auto path = Util::getWindowProcessPath(hwnd); // TODO 比较耗时，最好仅在单次show期间缓存，同时避免hwnd复用造成缓存错误
    // TODO 不能让winActiveOrder无限增长，需要定时清理
    winActiveOrder_.record(path, hwnd, QDateTime::currentDateTime());

    auto sourceStr = QMetaEnum::fromType<ForegroundChangeSource>().valueToKey(source);
    qDebug() << qUtf8Printable(QString("*ForeWin changed (%1):").arg(sourceStr)) // qUtf8Printable removes quotes ""
            << Util::getWindowTitle(hwnd) << Util::getClassName(hwnd) << path << Util::getFileDescription(path);
} // TODO 控制面板 和 资源管理器 exe是同一个，如何区分图标

/// collect, filter, sort Windows for presentation
QList<WindowGroup> Widget::prepareWindowGroupList() {
    QMap<QString, WindowGroup> winGroupMap;
    const auto list = Util::listValidWindows();
    for (auto hwnd: list) {
        if (hwnd == this->hWnd()) continue; // skip self
        auto path = Util::getWindowProcessPath(hwnd);
        if (path.isEmpty()) continue; // TODO 可能需要管理员权限
        auto& winGroup = winGroupMap[path];
        if (winGroup.exePath.isEmpty()) { // QIcon::isNull 判断可能不太准（例如空图标）
            winGroup.exePath = path;
            auto icon = Util::getCachedIcon(path, hwnd); // TODO background thread
            if (path.endsWith("QQ\\bin\\QQ.exe", Qt::CaseInsensitive)) { // draw chat partner for classical QQ
                QPixmap overlay = Util::getWindowIcon(hwnd);
                const auto iSize = lw->iconSize();
                QPixmap bgPixmap = icon.pixmap(iSize);
                icon = Util::overlayIcon(bgPixmap, overlay, {{iSize.width() / 2, iSize.height() / 2}, iSize / 2});
            }
            winGroup.icon = icon;
        }
        winGroup.addWindow({Util::getWindowTitle(hwnd), Util::getClassName(hwnd), hwnd});
    }
    auto winGroupList = winGroupMap.values();
    // 按照活跃度排序
    if (cfg.getNewAppSortSecond()) {
        // 启用"新应用排第二"：无使用记录的窗口组放到第2位（紧跟当前前台窗口）
        QList<WindowGroup> withRecord, withoutRecord;
        for (auto& group : winGroupList) {
            if (winActiveOrder_.lastValidActive(group.exePath, hwndsOfGroup(group)).second.isValid())
                withRecord.append(group);
            else
                withoutRecord.append(group);
        }
        std::sort(withRecord.begin(), withRecord.end(), [this](const WindowGroup& a, const WindowGroup& b) {
            return winActiveOrder_.lastValidActive(a.exePath, hwndsOfGroup(a)).second >
                   winActiveOrder_.lastValidActive(b.exePath, hwndsOfGroup(b)).second;
        });
        winGroupList = withRecord;
        if (!withoutRecord.isEmpty()) {
            // 插入到 index 1（第2位），保持无记录项之间的原有顺序
            for (int i = 0; i < withoutRecord.size(); i++)
                winGroupList.insert(1 + i, withoutRecord[i]);
        }
    } else {
        std::sort(winGroupList.begin(), winGroupList.end(), [this](const WindowGroup& a, const WindowGroup& b) {
            auto timeA = winActiveOrder_.lastValidActive(a.exePath, hwndsOfGroup(a)).second;
            auto timeB = winActiveOrder_.lastValidActive(b.exePath, hwndsOfGroup(b)).second;
            if (timeA.isNull() && timeB.isNull()) return false;
            if (timeA.isValid() && timeB.isValid()) return timeA > timeB;
            return timeA.isValid();
        });
    }
    return winGroupList;
}

bool Widget::prepareListWidget() {
    auto winGroupList = prepareWindowGroupList();
    lw->clear();
    for (auto& winGroup: winGroupList) {
        auto item = new QListWidgetItem(winGroup.icon, {}); // null != "", which will completely hide text area
        item->setData(Qt::UserRole, QVariant::fromValue(winGroup));
        item->setSizeHint(lw->gridSize()); // 决定了delegate的绘制区域，比grid小的话，paintRect就不居中了，而且update也不及时
//        item->setFlags(item->flags() & ~Qt::ItemIsSelectable); // 不可选中
        lw->addItem(item);
    }

    // calculate Geometry
    if (auto firstItem = lw->item(0)) {
        auto firstRect = lw->visualItemRect(firstItem);
        auto width = lw->gridSize().width() * lw->count() + (firstRect.x() - lw->frameWidth()); // 一些微小的噼里啪啦修正
        lw->setFixedWidth(width);

        // 几何：本程序唯一的居中逻辑在 layoutForCurrentMode()（Step 4b）
        if (!layoutForCurrentMode())
            return false;
    } else {
        // no item, hide ? TODO
        return false;
    }

    // set current item
    if (lw->count() >= 2) {
        // Alt+Shift+Tab: start from the last item instead of the second
        if (Util::isKeyPressed(VK_SHIFT)) {
            lw->setCurrentRow(lw->count() - 1);
        } else {
            auto foreWin = GetForegroundWindow();
            bool isFirstItemForeground = false;
            for (auto& info: winGroupList.at(0).windows) {
                if (info.hwnd == foreWin) {
                    isFirstItemForeground = true;
                    break;
                }
            }
            // 如果第一个item是前台窗口，就选中第二个
            // 因为有些情况：选中桌面 并不会产生一个item
            lw->setCurrentRow(isFirstItemForeground ? 1 : 0); //! 首次显示时，该行特别耗时：472ms
        }
    } else if (lw->count() == 1) {
        lw->setCurrentRow(0);
    }

    return true;
}

/// 按 DisplayMonitor 配置挑选目标屏幕；配置为跟随鼠标但取不到光标下屏幕时回退主屏
QScreen* Widget::targetScreen() {
    const bool displayOnPrimary = (cfg.getDisplayMonitor() == PrimaryMonitor);
    auto* screen = displayOnPrimary ? QGuiApplication::primaryScreen()
                                    : QGuiApplication::screenAt(QCursor::pos());
    if (!screen && !displayOnPrimary) { // fallback to primary screen
        qWarning() << "Cursor Screen nullptr! Fallback to primary";
        screen = QGuiApplication::primaryScreen();
    }
    return screen;
}

/// 本程序唯一的窗口几何计算处：按当前 mode_ 决定内容尺寸，再把窗口居中到目标屏幕
bool Widget::layoutForCurrentMode() {
    QWidget* content = nullptr;
    if (mode_ == Mode::AppMode) {
        groupList_->widget()->hide();
        lw->show();
        ui->label->show();
        content = lw;   // 宽度已由 prepareListWidget() 里的 setFixedWidth 设好
    } else {
        lw->hide();
        ui->label->hide();
        auto* view = groupList_->widget();
        view->setFixedSize(groupList_->preferredSize());
        view->show();
        content = view;
    }

    auto* screen = targetScreen();
    if (!screen) {
        qWarning() << "Screen nullptr!";
        sysTray.showMessage("Error", "Screen nullptr!");
        return false;
    }

    //region Fixed in Qt6, see commit [b927ee4b]
    //      !!!WARNING: 对于多屏幕，直接使用setGeometry or move会报错(QWindowsWindow::setGeometry: Unable to set geometry) & size显示不正确！
    //      报错时机为：从一个屏幕hide，再在另一个屏幕show; 第二次在同一个屏幕show，则正常
    //      size显示不正确不能忍，遂改用WinAPI
    //endregion
    qDebug() << "Screen:" << screen->name();
    auto contentRect = QRect(QPoint(0, 0), content->size());
    auto thisRect = contentRect.marginsAdded(ListWidgetMargin);
    thisRect.moveCenter(screen->geometry().center());
    this->setGeometry(thisRect);

    //region Fixed in Qt6, see commit [b927ee4b]
    //this->windowHandle()->setScreen(screen); // 若首次显示是在副屏，会导致size显示错误（如果没有这行）
    // `toNativePixels`是针对Point的，会根据屏幕原点进行位移
    // 对于其他类型（如Size），直接乘以`QHighDpiScaling::factor(screen)`即可
    //auto physicalPos = QHighDpi::toNativePixels(thisRect.topLeft(), screen);
    // 如果用SetWindowPos的话要注意加上`SWP_NOACTIVATE`，否则焦点有问题，没错，NoActive反而是Active (focus)的
    //SetWindowPos(hWnd(), nullptr, physicalPos.x(), physicalPos.y(), 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
    // !!!NOTE: 用WinAPI控制size貌似有问题，在图标增减的时候，无法正确调整Width，离子谱；只能用resize
    // 1. × 猜想是showNormal()恢复了原有的size，导致resize无效；但是改成show()也不行
    // 2. 猜想是隐藏状态改变size无效？（不科学吧），但是在`SetWindowPos`前show()好像会好一点（第二次显示调整size成功）aaa
    //this->resize(thisRect.size());
    //endregion

    contentRect.moveCenter(this->rect().center()); // local pos
    content->move(contentRect.topLeft());
    return true;
}

bool Widget::requestShow() { // TODO 当前台是开始菜单（Win）时，会导致显示 但无法操控
    mode_ = Mode::AppMode;
    userNavigated_ = false;
    return prepareListWidget() && forceShow();
}

/// 该进程的窗口，按最后使用时间降序（复用 Task 5 抽出的排序）
QList<WindowInfo> Widget::orderedGroupWindows(const QString& exePath) {
    QList<WindowInfo> result;
    for (auto hwnd: winActiveOrder_.orderedWindowsOf(exePath))
        result.append({Util::getWindowTitle(hwnd), Util::getClassName(hwnd), hwnd});
    return result;
}

/// 进入 GroupMode：用给定进程的窗口填充竖排列表，并定好初始高亮
bool Widget::enterGroupMode(const QString& exePath, int initialRow) {
    if (exePath.isEmpty()) return false;
    auto windows = orderedGroupWindows(exePath);
    if (windows.isEmpty()) return false;   // 没有窗口就不弹空列表

    mode_ = Mode::GroupMode;
    userNavigated_ = false;
    groupList_->fill(windows);
    groupList_->setCurrentRow(initialRow);
    if (!layoutForCurrentMode()) {
        mode_ = Mode::AppMode;       // 取不到屏幕就退回，不留半吊子状态
        groupList_->clear();
        return false;
    }
    return true;
}

/// 入口 A：Alt+` 独立按下（切换器不可见）——列表用「前台窗口所在进程」的窗口
/// 入口 B：切换器已可见时按 ` —— 列表用「当前选中项」所在进程的窗口（见函数开头）
bool Widget::requestShowGroup() {
    // 入口 B：AppMode 可见时按 ` —— 用「当前选中项」所在进程，而不是前台窗口
    // （此时前台窗口是本 Widget 自己，取它没有意义）
    if (mode_ == Mode::AppMode && this->isVisible()) {
        auto* cur = lw->currentItem();
        if (!cur) return false;
        auto exePath = cur->data(Qt::UserRole).value<WindowGroup>().exePath;
        if (exePath.isEmpty()) return false;
        // 初始高亮第 0 行 = 该组最后使用的窗口 = 不按 ` 直接松手会切到的那个。
        // 保持这条不变式：按 ` 再松手 == 什么都不做的松手。
        // 必须用返回值：丢掉它会让"陈旧 GroupMode"那一类 bug 复活（Task 7 的修复轮刚堵掉）
        if (!enterGroupMode(exePath, 0)) return false;
        return this->isVisible();
    }

    auto foreWin = GetForegroundWindow();
    auto exePath = Util::getWindowProcessPath(foreWin);
    if (exePath.isEmpty()) return false;

    auto order = winActiveOrder_.orderedWindowsOf(exePath);
    if (order.isEmpty()) return false;

    // 初始高亮 = 旧盲切的目标：前台窗口下标 +1（Shift 则 -1），端点循环。
    // 用下标算，不假设"前台窗口一定排首行"——那样高亮会偏。
    const int n = order.size();
    int idx = order.indexOf(foreWin);
    if (idx < 0) idx = 0;                       // 前台窗口不在过滤后的列表里，退化为首行
    const bool backward = Util::isKeyPressed(VK_SHIFT);
    const int delta = backward ? -1 : 1;
    const int initialRow = ((idx + delta) % n + n) % n;

    // 按 enterGroupMode() 的**返回值**判断，不能用 mode_ 的旧值：
    // 调用方原本可能就在 GroupMode（列表已隐藏），此时提前返回会让 mode_ 仍是 GroupMode，
    // 于是 forceShow() 会把上一条遗留的列表显示出来（且松手还会提交那行）
    if (!enterGroupMode(exePath, initialRow)) return false;

    // 几何算完还得真正把窗口显示出来：情形 A 的切换器原本是隐藏的，
    // 沿用 AppMode 那套防闪烁的 forceShow()（见设计文档 §2.4）。
    if (!this->isVisible())
        forceShow();
    return this->isVisible();
}

/// Alt 抬起时，GroupMode 的提交动作
void Widget::commitGroupSelection() {
    if (auto hwnd = groupList_->currentHwnd()) {
        Util::switchToWindow(hwnd);
        qInfo() << "(Alt+`)Switch to" << Util::getWindowTitle(hwnd) << Util::getClassName(hwnd);
    }
}

bool Widget::eventFilter(QObject* watched, QEvent* event) {
    if (mode_ == Mode::GroupMode && watched == groupList_->widget() && event->type() == QEvent::Wheel) {
        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        // 向上滚 = 上移
        groupList_->moveSelection(wheelEvent->angleDelta().y() > 0 ? -1 : +1);
        userNavigated_ = true;
        return true;
    }
    if (watched == lw && event->type() == QEvent::Wheel) {
        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        auto cursorPos = wheelEvent->position().toPoint();
        if (auto item = lw->itemAt(cursorPos)) {
            if (lw->currentItem() != item)
                lw->setCurrentItem(item);
            auto windowGroup = item->data(Qt::UserRole).value<WindowGroup>();
            if (windowGroup.windows.isEmpty()) return false;

            static QListWidgetItem* lastItem = nullptr;
            static HWND hwnd = nullptr;
            if (lastItem != item) { // Alt+Tab也可能造成切换; 每次show列表都是重新构建，所以item指针必然不同（即使同一个app）
                lastItem = item;
                hwnd = nullptr;
                groupWindowOrder.clear();
            }
            auto targetExe = windowGroup.exePath;
            static bool isLastRollUp = true;
            bool isRollUp = wheelEvent->angleDelta().x() > 0; // ListWidget的方向改成了从左到右，所以滚轮方向从y()变成x()了
            if (groupWindowOrder.isEmpty())
                groupWindowOrder = winActiveOrder_.orderedWindowsOf(targetExe); // TODO 其实这里不需要build 直接用lw里的就行...

            if (!hwnd) { // first time
                hwnd = groupWindowOrder.first(); // 选择最后活跃的窗口 TODO 考虑当前窗口就是First的情况，需要跳过，类似WinGroup
            } else { // select next window
                if (isLastRollUp == isRollUp) // 滚轮方向切换时，不轮换窗口
                    hwnd = rotateWindowInGroup(groupWindowOrder, hwnd, isRollUp);
            }
            isLastRollUp = isRollUp;

            HWND nextFocus = hwnd; // this隐藏后的焦点备选窗口, for `swtichToWindow` after AltUp
            if (isRollUp) {
                Util::bringWindowToTop(hwnd, this->hWnd()); // without activate
            } else {
                if (auto normal = rotateNormalWindowInGroup(groupWindowOrder, hwnd, false)) { // skip minimized
                    ShowWindow(normal, SW_SHOWMINNOACTIVE); // minimize
                    hwnd = normal;
                    nextFocus = hwnd;
                }
                if (auto normal = rotateNormalWindowInGroup(groupWindowOrder, hwnd, false))
                    nextFocus = normal; // 备选焦点切换为下一个非最小化窗口 after AltUp
            }
            notifyForegroundChanged(nextFocus, Inner);
            showLabelForItem(item, Util::getWindowTitle(nextFocus));
            qDebug() << "Wheel" << isRollUp << Util::getWindowTitle(nextFocus) << hwnd;

            return true; // stop propagation
        }
    }
    return false;
}

/// `forward`: true for restore, false for minimize
void Widget::rotateTaskbarWindowInGroup(const QString& exePath, bool forward, int windows) {
    qDebug() << "(Taskbar)Wheel on:" << exePath << forward << windows;
    if (exePath.isEmpty()) return;
    if (!windows) { // 程序没有打开的窗口，处于关闭状态; 若不拦截，可能造成错误窗口被触发：explorer.exe -> msedge.exe
        qDebug() << "No window for this app";
        return;
    }

    static QString lastPath;
    static HWND lastHwnd = nullptr;
    if (lastPath != exePath) {
        lastPath = exePath;
        groupWindowOrder.clear();
    }
    if (groupWindowOrder.isEmpty()) {
        groupWindowOrder = winActiveOrder_.orderedWindowsOf(exePath);
        lastHwnd = nullptr;
    }

    if (groupWindowOrder.isEmpty()) {
        qCritical() << "No window in group!" << exePath;
        // 有些软件的窗口是由子进程创建的，如 steam.exe -> steamwebhelper.exe (持有窗口)
        // 但是在任务栏只能获取到父进程steam.exe
        // 这种情况下，需要查找其子进程的路径
        auto childPaths = Util::getChildProcessPaths(exePath);
        if (childPaths.isEmpty()) return;
        if (childPaths.size() == 1) {
            qDebug() << "Try to switch to child process:" << childPaths.first();
            groupWindowOrder = winActiveOrder_.orderedWindowsOf(childPaths.first());
        } else {
            // 如果有多个子进程路径，就根据validWindows过滤
            qWarning() << "!Multiple child processes:" << childPaths;
            QSet<QString> validPaths;
            // If range-initializer returns a temporary, its lifetime is extended until the end of the loop
            for (auto hwnd: Util::listValidWindows()) {
                if (auto path = Util::getWindowProcessPath(hwnd); !path.isEmpty())
                    validPaths.insert(path.toLower());
            }
            for (auto& path: childPaths) {
                if (validPaths.contains(path.toLower())) {
                    qDebug() << "Try to switch to valid child process:" << path;
                    groupWindowOrder = winActiveOrder_.orderedWindowsOf(path);
                    break;
                }
            }
        }
        // TODO 有可能a进程开启b进程之后，a就关闭了，他俩也没有真的父子关系
        //  例如：ksolaunch.exe -> wps.exe
        //  此时只能通过File Description来匹配，均为“WPS Office”
        if (groupWindowOrder.isEmpty()) { // 无力回天
            qCritical() << "もうおしまいだ！";
            return;
        }
    }

    static bool isLastForward = true;
    HWND hwnd = nullptr;
    if (!lastHwnd) {
        hwnd = groupWindowOrder.first();
        if (forward && hwnd == GetForegroundWindow()) // 如果first是前台窗口且forward，则轮换下一个
            hwnd = rotateWindowInGroup(groupWindowOrder, hwnd, true);
    } else {
        if (isLastForward == forward)
            hwnd = rotateWindowInGroup(groupWindowOrder, lastHwnd, forward);
        else
            hwnd = lastHwnd;
    }
    isLastForward = forward;

    if (forward) {
        static auto mouseEvent = [](DWORD flag) {
            mouse_event(flag, 0, 0, 0, 0);
        };
        if (windows == 1) { // 由于过滤的存在，groupWindowOrder.size() 不一定等于 windows(真实窗口数量)
            // 单窗口情况下，模拟点击呼出，是最保险的
            if ((hwnd != GetForegroundWindow() || IsIconic(hwnd))) { // 若采用SW_SHOWMINNOACTIVE, 则前台窗口不会变化，可能为刚刚最小化的窗口
                mouseEvent(MOUSEEVENTF_LEFTDOWN);
                mouseEvent(MOUSEEVENTF_LEFTUP);
                qApp->processEvents();
                qDebug() << "(Taskbar)Switch by click";
            }
        } else {
            // 在TaskListThumbnailWnd显示的情况下restore window会导致预览实时刷新，导致卡顿和闪烁
            // 隐藏TaskListThumbnailWnd也无效，会自动show
            // DwmSetWindowAttribute[DWMWA_FORCE_ICONIC_REPRESENTATION, DWMWA_DISALLOW_PEEK], 效果都不好，还是会刷新闪烁

            // 只能采用偷鸡hack，按住左键的情况下，预览窗口会消失
            if (HWND thumbnail = Util::getCurrentTaskListThumbnailWnd(); IsWindowVisible(thumbnail)) {
                qDebug() << "(Taskbar)#Press LButton";
                mouseEvent(MOUSEEVENTF_LEFTDOWN);
                // 由于本程序hook了mouse，所以必须处理全局鼠标事件（in事件循环）
                QTimer::singleShot(20, this, [hwnd]() {
                    Util::switchToWindow(hwnd, true); // TODO thumbnail隐藏之前 不要switch，并且block滚轮 防止闪烁卡顿
                });
            } else
                Util::switchToWindow(hwnd, true);

            static QTimer* timer = [this]() {
                auto* timer = new QTimer;
                timer->setSingleShot(true);
                timer->setInterval(200);
                // TODO cursor移动后立即释放 防止拖拽
                timer->callOnTimeout(this, [this]() {
                    mouseEvent(MOUSEEVENTF_LEFTUP);
                    qDebug() << "(Taskbar)#Release LButton";

                    // 鼠标点击thumbnail之后，其获取焦点，此时若焦点在其窗口组成员中，thumbnail就不会隐藏，这是Windows机制
                    // 只能通过将焦点转移到Taskbar使其隐藏
                    // 直接 HIDE thumbnail 不太行，会导致之后restore窗口时 thumbnail刷新 + 窗口闪烁，闪瞎了
                    QTimer::singleShot(100, this, []() {
                        // 等待thumbnail显示
                        if (HWND thumbnail = Util::getCurrentTaskListThumbnailWnd(); IsWindowVisible(thumbnail)) {
                            if (HWND taskbar = FindWindow(L"Shell_TrayWnd", nullptr))
                                Util::switchToWindow(taskbar, true);
                        }
                    });
                });
                return timer;
            }();
            timer->stop();
            timer->start();
        }
        qDebug() << "(Taskbar)Switch to" << hwnd << Util::getWindowTitle(hwnd) << Util::getClassName(hwnd);
    } else {
        if (auto normal = rotateNormalWindowInGroup(groupWindowOrder, hwnd, false)) { // skip minimized
            if (normal != hwnd)
                qDebug() << "(Taskbar)Skip minimized" << hwnd << "->" << normal;
            hwnd = normal;
            ShowWindow(hwnd, SW_MINIMIZE); // SW_MINIMIZE 会让焦点自动回落到下一个窗口
            // 当所有窗口隐藏后，getElementUnderMouse() 会变成"CEF-OSC-WIDGET"，但是焦点和前台窗口并不是他，离谱
            // 此时Automation对鼠标下任务栏Element的判定会出错，solution为手动变焦到任务栏（见TaskbarWheelHooker.cpp）
            // SW_SHOWMINNOACTIVE不会切换焦点，即便本窗口已经最小化，但仍然持有焦点；但这不是合理的行为，同时会让QQ Follower反复弹出
            qDebug() << "(Taskbar)Minimize" << hwnd << Util::getWindowTitle(hwnd) << Util::getClassName(hwnd);
        }
    }

    lastHwnd = hwnd;
}

/// select next(forward)(older) or prev window in group<br>
/// Do nothing, but select HWND
HWND Widget::rotateWindowInGroup(const QList<HWND>& windows, HWND current, bool forward) {
    const auto N = windows.size();
    if (N == 1) return windows.first();
    for (int i = 0; i < N; i++) {
        if (windows.at(i) == current) {
            auto next_i = forward ? (i + 1) : (i - 1);
            auto next = windows.at((next_i + N) % N);
            return next;
        }
    }
    return nullptr;
}

/// Select next (including `current`) normal (!minimized) window in group<br>
/// return nullptr if all minimized
HWND Widget::rotateNormalWindowInGroup(const QList<HWND>& windows, HWND current, bool forward) {
    for (int i = 0; IsIconic(current) && i < windows.size(); i++) // skip minimized
        current = rotateWindowInGroup(windows, current, forward);
    return IsIconic(current) ? nullptr : current;
}

void Widget::clearGroupWindowOrder() {
    groupWindowOrder.clear();
}
