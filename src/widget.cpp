#include "../header/widget.h"
#include "ui_Widget.h"
#include "utils/Util.h"
#include "utils/Diag.h"
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
#include <QHideEvent>
#include <QMetaEnum>
#include "utils/SystemTray.h"
#include "utils/ConfigManager.h"
#include "utils/WindowGeometry.h"

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
        // 注意：Tab 不在这里。它在 GroupMode 下已被截走（"回应用切换器"），
        // 不是组内移动键。真落进来的话 delta=0，什么都不做 —— 这是安全的默认
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
    Diag::setSelfHwnd(hWnd()); // 诊断日志的 selfVisible 字段用

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
    //! 装在 QAbstractScrollArea **自己**身上的 filter 收不到真实的鼠标事件：项都画在 viewport 上，
    //! viewport 才是命中测试的结果、也由它消费。必须单独装在 viewport 上（见报告 §9.4 的探针：
    //! 真实点击只到 viewport，lw 与父窗口都看不到；直接 notify 给 lw 能看到、但那一次不会改选中）
    lw->viewport()->installEventFilter(this);

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
    // 滚轮移动高亮（见 Step 2b）。同样必须装在 viewport 上：滚轮先投递给 viewport，
    // 只有当它**没消费**（该方向滚不动）时才冒泡到滚动区对象 —— 装在对象上会漏掉另一半方向
    groupList_->widget()->installEventFilter(this);
    groupList_->widget()->viewport()->installEventFilter(this);
    setupGroupListFont();   // [group_list] 的字号/字体；与 setupLabelFont 同款，配置编辑后重应用
    setupThumbnail();       // [group_list] thumbnail=true 时右侧的 DWM 实时预览（宿主 + 60ms 合并定时器）

    watcher_ = new WindowWatcher(this);
    connect(watcher_, &WindowWatcher::newWindowsFound, this, &Widget::onNewWindows);
    connect(watcher_, &WindowWatcher::windowsGone, this, &Widget::onWindowsGone);
    // 同线程连接（Widget 拥有 WindowWatcher），所以 QList<HWND> 不需要 qRegisterMetaType
}

Widget::~Widget() {
    // 先注销 DWM 缩略图，再让 Qt 销毁宿主窗口：反过来会对已销毁的 HWND 调 DwmUnregisterThumbnail
    delete thumbnail_;
    thumbnail_ = nullptr;
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
    if (key == Qt::Key_Tab && mode_ == Mode::GroupMode && this->isVisible()) {
        // 与入口 B 对称：组列表里按 Alt+Tab 回到应用切换器（高亮停在组列表所属的那个应用）
        exitGroupModeToApp();
        return;
    }
    if (key == Qt::Key_Tab && mode_ == Mode::AppMode) { // switch to next or prev
        userNavigated_ = true;
        auto i = lw->currentRow();
        bool isShiftPressed = (modifiers & Qt::ShiftModifier);
        // weird formula, but works (hhh)
        auto index = (i - (2 * isShiftPressed - 1) + lw->count()) % lw->count();
        lw->setCurrentRow(index);
    } else if (key == Qt::Key_QuoteLeft && (modifiers & Qt::AltModifier)) {
        const int delta = (modifiers & Qt::ShiftModifier) ? -1 : 1;
        if (mode_ == Mode::GroupMode && this->isVisible()) {
            // 情形 C：已在组列表内 —— 移动高亮（与方向键 / Vim HJKL / 滚轮同义）。
            // 注意：**不**回到应用切换器。用户试过之后明确要它保持组内移动，
            // 回应用列表那条路留给 Tab（见本函数开头那一支）
            groupList_->moveSelection(delta);
            userNavigated_ = true;
        } else {
            // 情形 A（不可见）/ 情形 B（AppMode 可见，由 Task 8 接管）
            requestShowGroup();
        }
        return;
    } else if (mode_ == Mode::AppMode && (key == Qt::Key_Up || key == Qt::Key_Down)) {
        userNavigated_ = true;
        if (auto item = lw->currentItem()) {
            auto center = lw->visualItemRect(item).center();
            // 转发映射到WheelEvent
            auto wheelEvent = new QWheelEvent(center, lw->mapToGlobal(center), {},
                                              {key == Qt::Key_Up ? 120 : -120, 0},
                                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::postEvent(lw, wheelEvent);
        }
    } else if (mode_ == Mode::AppMode && (key == Qt::Key_Left || key == Qt::Key_Right)) { // 默认情况下 左右键可以切换item 只需要处理边界循环即可
        userNavigated_ = true;
        const int N = lw->count();
        const int i = lw->currentRow();
        if (key == Qt::Key_Left && i == 0)
            lw->setCurrentRow(N - 1);
        else if (key == Qt::Key_Right && i == N - 1)
            lw->setCurrentRow(0);
    } else if (mode_ == Mode::AppMode && VimArrows.contains(key)) { // map [K J H L] to [↑ ↓ ← →]
        userNavigated_ = true;
        QApplication::postEvent(lw, new QKeyEvent(QEvent::KeyPress, VimArrows.value(key), modifiers));
    } else if (mode_ == Mode::GroupMode) {
        // GroupMode 里所有剩下的键都归这一支：设计文档 §2.3 把方向键 / Vim H J K L 定义成
        // "移动高亮"（Tab 已改为"回应用切换器"，见函数开头那一支；` 仍走上面自己的分支）。
        // 放在最后且不判 key，是为了保证**任何**键都不会落进上面那些分支——
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

/// 组列表（Alt+`）的字号/字体。与 setupLabelFont() 同款：构造时应用一次，配置编辑后重应用。
/// 两个键都可选，**都不写就不动字体**（沿用继承来的默认字体 = 既有观感）：
/// 行高与宽度上限由 GroupWindowList::applyFont() 按新字体的度量派生，
/// 默认字体下派生结果与改动前逐像素一致（真机实测见 docs/2026-10-07-group-list-font-design.md §3.2）
void Widget::setupGroupListFont() {
    const QFont base = groupList_->widget()->font();   // 基准只取一次，反复 apply 不会拿上一次的结果当基准
    const QStringList Fallback = {"Microsoft YaHei UI", "Microsoft YaHei", "Consolas"}; // fallback
    auto reloadGroupListFontCfg = [this, base, Fallback] {
        QFont font = base;
        const int size = cfg.get("group_list/font_size", -1).toInt();
        if (size > 0) font.setPointSize(size);   // <=0 / 非数字：当成"没配"，不给 0pt 的不可见字
        const auto family = cfg.get("group_list/font_family", QString()).toString();
        if (!family.isEmpty()) font.setFamilies(QStringList{family} << Fallback);
        groupList_->applyFont(font);
        qDebug() << "Group list font:" << font.families() << "-> actual" << QFontInfo(font).family();
    };
    reloadGroupListFontCfg();

    // auto reload
    connect(&cfg, &ConfigManager::configEdited, this, [reloadGroupListFontCfg] {
        reloadGroupListFontCfg();
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
                        // 多屏选项 2（AllMoveToCurrent）：先搬到当前屏幕再切过去。
                        // 目标屏幕取不到时不动窗口，至少别把切换本身也弄丢
                        if (cfg.getMonitorSwitchMode() == MonitorSwitchMode::AllMoveToCurrent) {
                            if (auto* screen = targetScreen())
                                Util::moveWindowToMonitor(targetWin.hwnd, Util::screenHandle(screen));
                        }
                        Diag::log("#SWITCH", targetWin.hwnd);
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
    Diag::log("#FG", hwnd);
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
    // 多屏选项 1（CurrentOnly）：只收当前屏幕上的窗口。目标屏幕或窗口所在屏幕取不到时
    // 不过滤 —— 宁可多显示，也别因为一次取不到句柄就把列表清空
    const bool currentOnly = (cfg.getMonitorSwitchMode() == MonitorSwitchMode::CurrentOnly);
    const HMONITOR targetMonitor = currentOnly ? Util::screenHandle(targetScreen()) : nullptr;
    const auto list = Util::listValidWindows();
    for (auto hwnd: list) {
        if (hwnd == this->hWnd()) continue; // skip self
        if (currentOnly && targetMonitor && Util::monitorOfWindow(hwnd) != targetMonitor) continue;
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

/// `lw` 的宽度公式（原本内联在 prepareListWidget() 里，Task 9 复审抽出）：
/// 列表是 IconMode + LeftToRight + setWrapping(false) + 滚动条 AlwaysOff，
/// 宽度必须正好是"项数 × 栅格宽 + 修正"，否则多出来的项落在可视区外、少了的项留下空白。
/// 三项变化：prepareListWidget()（全量重建）、onNewWindows()（插入）、onWindowsGone()（移除）
void Widget::updateListWidth() {
    if (auto firstItem = lw->item(0)) {
        auto firstRect = lw->visualItemRect(firstItem);
        auto width = lw->gridSize().width() * lw->count() + (firstRect.x() - lw->frameWidth()); // 一些微小的噼里啪啦修正
        lw->setFixedWidth(width);
    }
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
    if (!lw->item(0)) {
        // no item, hide ? TODO
        return false;
    }
    updateListWidth();

    // 几何：本程序唯一的居中逻辑在 layoutForCurrentMode()（Step 4b）
    if (!layoutForCurrentMode())
        return false;

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

/// 按 DisplayMonitor 配置挑选目标屏幕；配置为跟随鼠标但取不到光标下屏幕时回退主屏。
/// 本次弹出已经选过屏就直接复用（pinnedScreen_）：动态入列会在弹出期间反复重算几何，
/// 而跟随鼠标模式下 `screenAt(QCursor::pos())` 一次一个答案 —— 用户中途把鼠标移到另一个
/// 显示器上，弹窗就会跟着跳过去，这是本次改动之前不存在的行为
QScreen* Widget::targetScreen() {
    if (pinnedScreen_) return pinnedScreen_;

    const bool displayOnPrimary = (cfg.getDisplayMonitor() == PrimaryMonitor);
    auto* screen = displayOnPrimary ? QGuiApplication::primaryScreen()
                                    : QGuiApplication::screenAt(QCursor::pos());
    if (!screen && !displayOnPrimary) { // fallback to primary screen
        qWarning() << "Cursor Screen nullptr! Fallback to primary";
        screen = QGuiApplication::primaryScreen();
    }
    // 取不到就不钉，下次调用再试
    if (screen) pinnedScreen_ = screen;
    return screen;
}

/// 本程序唯一的窗口几何计算处：按当前 mode_ 决定内容尺寸，再把窗口居中到目标屏幕
bool Widget::layoutForCurrentMode() {
    QSize contentSize;
    if (mode_ == Mode::AppMode) {
        hideThumbnail();   // 缩略图只在 GroupMode 出现；AppMode 下强制隐藏
        groupList_->widget()->hide();
        lw->show();
        ui->label->show();
        contentSize = lw->size();   // 宽度由 updateListWidth() 设好（prepareListWidget / onNewWindows / onWindowsGone）
    } else {
        lw->hide();
        ui->label->hide();
        auto* view = groupList_->widget();
        bool thumbOn = cfg.getGroupThumbnailEnabled();
        const QSize thumbBox = thumbOn ? thumbnailBoxSize() : QSize();
        const int thumbBoxW = thumbBox.width();
        const int thumbBoxH = thumbBox.height();
        // 字号放大后宽度上限跟着放宽（GroupWindowList::effectiveMaxWidth），但不能超出屏幕：
        // 下面只做居中、没有任何地方兜宽度，超宽会被裁在屏幕两侧。
        // 必须在 preferredSize() 之前设：setWidthCap() 会顺带重排每一行的 sizeHint。
        // 缩略图开启时，屏幕兜底还要扣掉缩略图盒与间距，否则「列表 + 缩略图」整体会超出屏幕
        if (auto* screen = targetScreen()) {
            const auto avail = screen->availableGeometry().width();
            const int availContentW = avail - ListWidgetMargin.left() - ListWidgetMargin.right();
            int cap = availContentW;
            if (thumbOn) cap -= thumbBoxW + ThumbnailGap;
            groupList_->setWidthCap(qMax(GroupWindowList::MinWidth, cap));
            // 屏幕窄到连「列表下限 + 间距 + 缩略图」都放不下时，整块隐藏缩略图；
            // 绝不把缩略图钳到右缘压在列表上（用户 2026-10-18 的决策）
            if (thumbOn && GroupWindowList::MinWidth + ThumbnailGap + thumbBoxW > availContentW) {
                thumbOn = false;
                qWarning() << "Thumbnail hidden: not enough horizontal room";
            }
        }
        const QSize listSize = groupList_->preferredSize();
        view->setFixedSize(listSize);
        view->show();
        // 内容区 = 列表 + 间距 + 缩略图盒；高度取两者较大值，列表与缩略图各自垂直居中。
        // 预留的是**配置的上限盒**而不是当前源的等比尺寸：弹窗宽度不该随高亮移动而变化
        contentSize = thumbOn
                      ? QSize(listSize.width() + ThumbnailGap + thumbBoxW, qMax(listSize.height(), thumbBoxH))
                      : listSize;
        if (thumbOn) {
            // 弹窗位置/尺寸变了：先把宿主藏起来（保留 DWM 注册），等 60ms 合并定时器
            // 重新定位并显示，免得它停在旧位置当幽灵 —— 「组列表先出现，缩略图随后填入」
            if (thumbnailHost_) thumbnailHost_->hide();
            scheduleThumbnailUpdate();
        } else {
            hideThumbnail();
        }
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
    auto contentRect = QRect(QPoint(0, 0), contentSize);
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
    if (mode_ == Mode::GroupMode) {
        // 列表在内容区里垂直居中（缩略图由定时器另行定位到右侧）
        auto* view = groupList_->widget();
        view->move(contentRect.topLeft() + QPoint(0, (contentRect.height() - view->height()) / 2));
    } else {
        lw->move(contentRect.topLeft());
    }
    return true;
}

/// 缩略图预留盒的实际尺寸（逻辑像素）：配置值（默认 320×180，已钳在 80..2000 / 60..2000），
/// 再被屏幕可用区域兜一次 —— 配置得再大，也不能让「列表 + 间距 + 缩略图」超出屏幕被裁掉
QSize Widget::thumbnailBoxSize() {
    int w = cfg.getGroupThumbnailWidth();
    int h = cfg.getGroupThumbnailHeight();
    if (auto* screen = targetScreen()) {
        const QSize avail = screen->availableGeometry().size();
        const int maxW = avail.width() - ListWidgetMargin.left() - ListWidgetMargin.right()
                         - ThumbnailGap - GroupWindowList::MinWidth;
        const int maxH = avail.height() - ListWidgetMargin.top() - ListWidgetMargin.bottom();
        w = qMin(w, qMax(80, maxW));
        h = qMin(h, qMax(60, maxH));
    }
    return {w, h};
}

/// 组列表右侧的 DWM 缩略图：宿主 + 封装 + 合并定时器，构造时创建一次。
/// 宿主是**独立顶层窗口**（owner = 主弹窗）：DWM 的窗口圆角只能裁整个顶层窗口，
/// 主弹窗是整窗圆角，没法只圆缩略图那一块。置顶、不抢焦点、输入穿透、半透明背景 ——
/// 内容完全由 DWM 在宿主上合成，Qt 不画任何东西
void Widget::setupThumbnail() {
    thumbnailHost_ = new QWidget(this);
    thumbnailHost_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                                   Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
    thumbnailHost_->setAttribute(Qt::WA_TranslucentBackground);
    thumbnailHost_->setAttribute(Qt::WA_ShowWithoutActivating);
    thumbnailHost_->hide();
    // 立即创建原生窗口：DwmThumbnail 构造需要宿主 HWND。
    // 没有标题、且 Qt::Tool 带 WS_EX_TOOLWINDOW，不会被 listValidWindows()/WindowWatcher
    // 当成应用窗口收进切换器
    thumbnail_ = new DwmThumbnail(reinterpret_cast<HWND>(thumbnailHost_->winId()));

    thumbnailTimer_ = new QTimer(this);
    thumbnailTimer_->setSingleShot(true);
    thumbnailTimer_->setInterval(60);   // 切换优先、缩略图延迟：高亮连按只处理最后一次
    connect(thumbnailTimer_, &QTimer::timeout, this, &Widget::updateThumbnail);

    // 高亮变化（键盘 / 滚轮 / 鼠标点击 / 自动选中）一律走同一条合并路径
    connect(groupList_->widget(), &QListWidget::currentItemChanged, this, [this] {
        scheduleThumbnailUpdate();
    });
    // 配置编辑：开关 / 宽高变化后重新布局（重算内容宽度并重新调度缩略图）
    connect(&cfg, &ConfigManager::configEdited, this, [this] {
        if (mode_ == Mode::GroupMode && this->isVisible())
            layoutForCurrentMode();
        else
            hideThumbnail();
    });
}

void Widget::scheduleThumbnailUpdate() {
    if (!thumbnailTimer_) return;
    if (mode_ != Mode::GroupMode || !cfg.getGroupThumbnailEnabled()) {
        hideThumbnail();
        return;
    }
    thumbnailTimer_->start();   // 已在跑就重启：只处理最后一次高亮变化
}

void Widget::hideThumbnail() {
    if (thumbnailTimer_) thumbnailTimer_->stop();
    if (thumbnail_) thumbnail_->hide();     // 只关可见、保留注册，下次可直接复用
    if (thumbnailHost_ && thumbnailHost_->isVisible()) thumbnailHost_->hide();
}

void Widget::updateThumbnail() {
    if (!thumbnail_ || !thumbnailHost_) return;
    if (mode_ != Mode::GroupMode || !this->isVisible() || !cfg.getGroupThumbnailEnabled()) {
        hideThumbnail();
        return;
    }

    const HWND source = groupList_->currentHwnd();
    // cloaked 的源窗口 DWM 没有可靠内容，隐藏；最小化窗口仍然显示预览 ——
    // 用户 2026-10-18 的决定：这块预览就是用来帮忙找窗口的，最小化的也要能看到
    if (!source || Util::isWindowCloaked(source)) {
        hideThumbnail();
        return;
    }

    // 等比缩放用**客户区**尺寸（与 DwmThumbnail 的 fSourceClientAreaOnly=TRUE 对齐）。
    // 应用自绘的标题栏会留在客户区里 —— 这是「客户区」的边界，不是 DWM 的边框
    RECT sourceClient{};
    if (!GetClientRect(source, &sourceClient) ||
        sourceClient.right <= sourceClient.left || sourceClient.bottom <= sourceClient.top) {
        hideThumbnail();
        return;
    }
    const QSize box = thumbnailBoxSize();
    const auto fitted = WindowGeometry::fitAspect(sourceClient.right - sourceClient.left,
                                                  sourceClient.bottom - sourceClient.top,
                                                  box.width(), box.height());
    if (fitted.width <= 0 || fitted.height <= 0) {
        hideThumbnail();
        return;
    }

    // 与 layoutForCurrentMode() 同一套内容尺寸：列表宽 + 间距 + 预留盒宽，高度取较大值。
    // 缩略图在预留盒里水平居中、相对内容区垂直居中。gap 传 0：
    // 间距与盒内居中偏移都已经算进 listRect 的宽度里
    const QSize listSize = groupList_->preferredSize();
    const QSize contentSize(listSize.width() + ThumbnailGap + box.width(),
                            qMax(listSize.height(), box.height()));
    const WindowGeometry::Rect listRect{
        0, 0, listSize.width() + ThumbnailGap + (box.width() - fitted.width) / 2, contentSize.height()};
    const auto thumbRect = WindowGeometry::thumbnailRect(listRect, fitted.width, fitted.height, 0,
                                                         contentSize.width(), contentSize.height());

    // 内容区在弹窗里的本地原点（layoutForCurrentMode() 把它居中），再换算到全局逻辑坐标
    const QPoint contentTopLeft((this->width() - contentSize.width()) / 2,
                                (this->height() - contentSize.height()) / 2);
    const QPoint globalLogical = mapToGlobal(contentTopLeft + QPoint(thumbRect.x, thumbRect.y));

    // DWM 目的地与 Win32 窗口坐标都是**物理像素**（f3）。
    // 不能把 Qt 的全局逻辑坐标直接乘 devicePixelRatioF()：多屏时 Qt 的逻辑坐标是各屏按自己的
    // DPR 拼接出来的，副屏原点/缩放率不同就会算偏（2026-10-18 副屏实测）。
    // 改用主弹窗的**客户区物理原点** + 窗口自身 DPR 换算，与屏幕拓扑、缩放率无关。
    POINT clientOrigin{0, 0};
    if (!ClientToScreen(this->hWnd(), &clientOrigin)) {
        hideThumbnail();
        return;
    }
    const qreal dpr = this->devicePixelRatioF();
    const int physX = clientOrigin.x + qRound((contentTopLeft.x() + thumbRect.x) * dpr);
    const int physY = clientOrigin.y + qRound((contentTopLeft.y() + thumbRect.y) * dpr);
    const int physW = qRound(thumbRect.width * dpr);
    const int physH = qRound(thumbRect.height * dpr);
    if (physW <= 0 || physH <= 0) {
        hideThumbnail();
        return;
    }

    // 先给 Qt 一份正确的逻辑几何（首次 show 时不会闪到屏幕别处），再用 SetWindowPos 落物理坐标。
    // 宿主从不接收输入/焦点（WA_ShowWithoutActivating + WindowDoesNotAcceptFocus + 输入穿透）
    thumbnailHost_->setGeometry(QRect(globalLogical, QSize(thumbRect.width, thumbRect.height)));
    if (!thumbnailHost_->isVisible()) thumbnailHost_->show();

    const HWND hostHwnd = reinterpret_cast<HWND>(thumbnailHost_->winId());
    // SWP_NOACTIVATE：摆缩略图不能把焦点从用户正在切换的应用手里抢走
    SetWindowPos(hostHwnd, HWND_TOPMOST, physX, physY, physW, physH, SWP_NOACTIVATE);
    applyThumbnailHostRegion();

    // 源 hwnd 未变则跳过注册（setSource() 自己也会短路，这里省掉一次调用）
    if (thumbnail_->source() != source) {
        if (!thumbnail_->setSource(source)) {
            hideThumbnail();   // 注册失败只静默降级为不显示，不影响窗口切换
            return;
        }
    }

    // 宿主客户区就是预览区：物理像素矩形直接交给 DWM
    RECT hostClient{};
    if (!GetClientRect(hostHwnd, &hostClient)) {
        hideThumbnail();
        return;
    }
    const RECT destination{0, 0, hostClient.right, hostClient.bottom};
    thumbnail_->setDestinationRect(destination);
}

void Widget::applyThumbnailHostRegion() {
    if (!thumbnailHost_) return;
    const HWND hwnd = reinterpret_cast<HWND>(thumbnailHost_->winId());

    if (!thumbnailCornerTried_) {
        thumbnailCornerTried_ = true;
        thumbnailNativeCorner_ = Util::setWindowRoundCorner(hwnd);
        if (!thumbnailNativeCorner_)
            qWarning() << "DWM round corner unavailable, fallback to SetWindowRgn";
    }
    if (thumbnailNativeCorner_) return;   // Win11：DWM 圆角随窗口尺寸自动生效

    // Win10 回退：窗口区域圆角；SetWindowRgn 成功后区域归系统所有，失败要自己删
    RECT client{};
    if (!GetClientRect(hwnd, &client) || client.right <= 0 || client.bottom <= 0) return;
    const HRGN region = CreateRoundRectRgn(0, 0, client.right + 1, client.bottom + 1, 16, 16);
    if (region && !SetWindowRgn(hwnd, region, TRUE))
        DeleteObject(region);
}

bool Widget::requestShow() { // TODO 当前台是开始菜单（Win）时，会导致显示 但无法操控
    mode_ = Mode::AppMode;
    userNavigated_ = false;
    pinnedScreen_ = nullptr;   // 新的一次弹出：重新按配置选屏（见 targetScreen()）
    // 记录「出发应用」：入口 B（切换器已可见时按 `）用它决定展示哪一组窗口。
    // 只在**本窗口还没显示**时取 —— requestShow() 会被 main.cpp 的重试循环和"重复按 Alt+Tab"
    // 调用，那时前台已经是本窗口自己，取到的没有意义。不可接受的窗口（桌面、系统过渡窗口）不记，
    // 免得把出发应用记成一个迟早解析不出进程路径的东西 —— 入口 B 会退回高亮项那份旧定义。
    if (!this->isVisible()) {
        if (auto fg = GetForegroundWindow(); fg != this->hWnd() && Util::isWindowAcceptable(fg))
            foregroundAtShow_ = fg;
    }
    const bool shown = prepareListWidget() && forceShow();

    // 判据是"列表此刻在不在屏幕上"，不是 forceShow() 的返回值 —— 后者只表示"抢到前台"。
    // main.cpp:77-88 的 retry 循环最多试 5 次，可能**窗口已可见**却没抢到前台就放弃；
    // 用 shown 当判据会让那条路径上的 watcher 根本没启动（A 开着也不轮询）。
    if (this->isVisible()) {
        if (cfg.getWatchNewWindows()) {
            // 先 stop() 再 start()：`WindowWatcher::start()` 里的 seed() 是无条件执行的，
            // 对**正在运行**的 watcher 裸调 start() 会把"上一 tick 之后新建的窗口"吃进 seed，
            // 那些窗口再也不会被报出来（静默丢通知）。requestShow() 确实可能被重复调用
            // （main.cpp:77-88 的 retry 循环、或焦点被抢走后重按 Alt+Tab），所以不能假设此处是停止态。
            // 此刻 prepareListWidget() 刚把列表整体重建过，重新播种与列表内容一致，不会漏报。
            watcher_->stop();
            watcher_->start();
        } else {
            watcher_->stop();
        }
    }
    return shown;
}

/// 列表一旦离开屏幕就不再轮询。覆写 hideEvent 而不是逐个 hide() 调用点，是因为 hide 的路径
/// 不止一处（keyReleaseEvent、焦点丢失、requestShow 后的各种收尾），漏一处就会留下一只
/// 后台不停枚举窗口的定时器
void Widget::hideEvent(QHideEvent* event) {
    watcher_->stop();
    pinnedScreen_ = nullptr;   // 本次弹出结束，屏幕钉扎作废
    hideThumbnail();           // 停掉待处理定时器并隐藏缩略图与宿主（DWM 注册保留）
    QWidget::hideEvent(event);
}

void Widget::onNewWindows(const QList<HWND>& added) {
    // 守卫必须放宽成"只判可见性"，且模式分支要放在它**之后、AppMode 逻辑之前**：
    // 原来那道守卫要求当前必须是 AppMode，会先把 GroupMode 挡回去，
    // 于是下面这个分支成了死代码（本 Task 的顺序陷阱）
    if (!this->isVisible()) return;

    // 多屏选项 1（CurrentOnly）：动态入列同样只收当前屏幕的窗口，
    // 否则列表刚过滤完，新窗口又会从别的屏幕漏进来
    const bool currentOnly = (cfg.getMonitorSwitchMode() == MonitorSwitchMode::CurrentOnly);
    const HMONITOR targetMonitor = currentOnly ? Util::screenHandle(targetScreen()) : nullptr;

    if (mode_ == Mode::GroupMode) {
        // 只关心本进程（本组）的新窗口，别的程序的窗口不进组列表
        int lastInserted = -1;
        for (auto hwnd: added) {
            if (currentOnly && targetMonitor && Util::monitorOfWindow(hwnd) != targetMonitor)
                continue;
            if (Util::getWindowProcessPath(hwnd).compare(currentGroupExePath_, Qt::CaseInsensitive) != 0)
                continue;
            // 插到列表末尾：新窗口没有使用记录，按最后使用时间降序它排最后
            WindowInfo info{Util::getWindowTitle(hwnd), Util::getClassName(hwnd), hwnd};
            lastInserted = groupList_->append(info);   // append() 内部已重算宽度（applyPreferredWidth）
        }
        if (lastInserted < 0) return;
        // 开关 B + 手动优先：本次弹出期间用户翻过组列表就不抢选中（与 AppMode 分支同一语义）
        if (cfg.getAutoSelectNew() && !userNavigated_)
            groupList_->setCurrentRow(lastInserted);
        // 行数变了，尺寸要重算：条目的宽度由 append() 自己管，而**窗口**的几何只在
        // layoutForCurrentMode() 里算（它按当前 mode_ 给 groupList 的视图 setFixedSize 并重新居中），
        // 整批调一次就够，不必每行调。这里不调 showLabelForItem()：GroupMode 下 label 是隐藏的
        // （layoutForCurrentMode() 里 ui->label->hide()），它算的是 lw 的项坐标，与本列表无关
        layoutForCurrentMode();
        return;
    }

    QList<QListWidgetItem*> newlyAdded;
    for (auto hwnd: added) {
        if (currentOnly && targetMonitor && Util::monitorOfWindow(hwnd) != targetMonitor)
            continue;
        auto path = Util::getWindowProcessPath(hwnd);   // 只为新窗口查，慢操作
        if (path.isEmpty()) continue;

        // 找已有的同进程项
        QListWidgetItem* existing = nullptr;
        for (int i = 0; i < lw->count(); ++i) {
            if (lw->item(i)->data(Qt::UserRole).value<WindowGroup>().exePath == path) {
                existing = lw->item(i);
                break;
            }
        }

        if (existing) {
            // 已有进程多了一个窗口：不新增项，只把窗口并进该组的窗口列表
            auto group = existing->data(Qt::UserRole).value<WindowGroup>();
            // 兜底去重：只信 diff 是不够的 —— 列表有可能在 tracker 还没来得及播种时就被整体重建
            // （requestShow() 里 prepareListWidget() 先跑、播种紧随其后），这时同一个 hwnd 会在
            // 下一 tick 被当成"新增"报上来，而它其实已经在组里了。重复项不会切错窗口
            // （松手时按 lastActive 命中），但会让组的窗口列表失真，顺手挡掉
            bool alreadyInGroup = false;
            for (auto& info: group.windows) {
                if (info.hwnd == hwnd) {
                    alreadyInGroup = true;
                    break;
                }
            }
            if (!alreadyInGroup) {
                group.addWindow({Util::getWindowTitle(hwnd), Util::getClassName(hwnd), hwnd});
                existing->setData(Qt::UserRole, QVariant::fromValue(group));
            }
        } else {
            int row = insertAppItem(path, hwnd);
            if (row >= 0) newlyAdded.append(lw->item(row));
        }
    }

    if (newlyAdded.isEmpty()) return;

    // 项数变了 ⇒ 列表宽度（以及窗口几何）必须跟着变：lw 不换行、无滚动条，
    // 追加在末尾的新项会落在可视区之外，而开关 B 关时没有任何代码会去滚动它
    updateListWidth();
    layoutForCurrentMode();

    // 开关 B + 手动优先：本次弹出期间用户翻过列表就不再抢
    if (cfg.getAutoSelectNew() && !userNavigated_)
        lw->setCurrentItem(newlyAdded.last());

    // 几何动过之后必须重新摆标签：showLabelForItem() 算的是窗口坐标，
    // 而 layoutForCurrentMode() 只挪 content（"标签在最后一次几何变化之后定位"这条不变式
    // 原本只靠调用顺序维持，弹出中途重新布局会打破它）
    showLabelForItem(lw->currentItem());
}

/// 沿用既有排序语义：无使用记录的新进程排到末尾；new_app_second 开启时插到第 2 位
int Widget::insertAppItem(const QString& exePath, HWND hwnd) {
    auto group = WindowGroup{};
    group.exePath = exePath;
    group.icon = Util::getCachedIcon(exePath, hwnd);   // 有缓存，不慢
    group.addWindow({Util::getWindowTitle(hwnd), Util::getClassName(hwnd), hwnd});

    auto* item = new QListWidgetItem(group.icon, {});
    item->setData(Qt::UserRole, QVariant::fromValue(group));
    item->setSizeHint(lw->gridSize());

    const bool intoSecond = cfg.getNewAppSortSecond() && lw->count() >= 1;
    const int row = intoSecond ? 1 : lw->count();
    lw->insertItem(row, item);
    return row;
}

bool Widget::removeEmptyGroups() {
    bool countChanged = false;
    for (int i = lw->count() - 1; i >= 0; --i) {
        auto group = lw->item(i)->data(Qt::UserRole).value<WindowGroup>();
        for (int j = group.windows.size() - 1; j >= 0; --j)
            if (!IsWindow(group.windows[j].hwnd))
                group.windows.removeAt(j);
        if (group.windows.isEmpty()) {
            const bool wasCurrent = (lw->currentRow() == i);
            delete lw->takeItem(i);
            countChanged = true;
            if (wasCurrent) lw->setCurrentRow(qBound(0, i, qMax(0, lw->count() - 1)));
        } else {
            lw->item(i)->setData(Qt::UserRole, QVariant::fromValue(group));
        }
    }
    return countChanged;
}

void Widget::onWindowsGone(const QList<HWND>& removed) {
    // 同上：本分支必须放在下面那道"必须是 AppMode"的守卫**之前**，否则 GroupMode 永远进不来
    if (mode_ == Mode::GroupMode && this->isVisible()) {
        bool changed = false;
        for (auto hwnd: removed) {
            const bool wasCurrent = (groupList_->currentHwnd() == hwnd);
            const int row = groupList_->removeByHwnd(hwnd);
            if (row < 0) continue;
            changed = true;
            if (wasCurrent) groupList_->setCurrentRow(row);   // 顺延到同下标（钳位在 setCurrentRow 内）
        }
        if (changed) layoutForCurrentMode();   // 行数变了才重算窗口几何，理由同 AppMode 分支
        return;
    }
    if (mode_ != Mode::AppMode || !this->isVisible()) return;
    // 项数没变就别重算几何：setGeometry 打在这扇半透明+模糊的窗口上是一次可见的重绘，
    // 而本函数每个 tick 都会被调
    if (!removeEmptyGroups()) return;

    updateListWidth();
    layoutForCurrentMode();
    showLabelForItem(lw->currentItem());   // 同上：几何动过，标签要重摆
}

/// 该进程的窗口，按最后使用时间降序（复用 Task 5 抽出的排序）
QList<WindowInfo> Widget::orderedGroupWindows(const QString& exePath) {
    QList<WindowInfo> result;
    // 多屏选项 1（CurrentOnly）：组列表里也只看当前屏幕的窗口；
    // 过滤后为空时 enterGroupMode() 会按既有逻辑返回 false（不弹空列表）
    const bool currentOnly = (cfg.getMonitorSwitchMode() == MonitorSwitchMode::CurrentOnly);
    const HMONITOR targetMonitor = currentOnly ? Util::screenHandle(targetScreen()) : nullptr;
    for (auto hwnd: winActiveOrder_.orderedWindowsOf(exePath)) {
        if (currentOnly && targetMonitor && Util::monitorOfWindow(hwnd) != targetMonitor) continue;
        result.append({Util::getWindowTitle(hwnd), Util::getClassName(hwnd), hwnd});
    }
    return result;
}

/// 进入 GroupMode：用给定进程的窗口填充竖排列表，并定好初始高亮
bool Widget::enterGroupMode(const QString& exePath, int initialRow) {
    if (exePath.isEmpty()) return false;
    auto windows = orderedGroupWindows(exePath);
    if (windows.isEmpty()) return false;   // 没有窗口就不弹空列表

    mode_ = Mode::GroupMode;
    currentGroupExePath_ = exePath;   // onNewWindows() 靠它只收本进程的新窗口
    userNavigated_ = false;
    // 组列表同样要轮询新窗口，否则 GroupMode 的动态扫描永远不会被触发：
    // requestShow() 里的启动只覆盖 AppMode，而入口 A 的弹窗是从隐藏状态 show 出来的，
    // hideEvent 已经把 watcher 停掉，没有人会替我们启动它。
    // 这里**不能**照抄 Task 9 的 `if (isVisible()) { stop(); start(); } else stop();`：
    // 那里的可见性判据表示"窗口已经抢到屏幕"，而 enterGroupMode() 被调用时入口 A 的窗口还没 show
    // （forceShow() 在调用方里、之后才跑），照抄会让入口 A 只 stop 不 start，
    // 整个组列表期间一次都不轮询 —— 正是本 Step 要防的"静默不生效"。
    // 所以判据取配置。这里的 stop() 只改定时器相位、不承担正确性：`WindowWatcher::start()` 会
    // 无条件重播 seed（WindowWatcher.cpp:27），而 seed() 整体替换 known_（WindowDiffTracker.h:53-55），
    // 所以删掉 stop() 不改变任何行为。真正必须的是这次重播：沿用上一条 Alt+Tab 播下的旧集合，
    // 会把「那个时刻之后出现、而此刻**已经**在组列表里的本进程窗口」在下一 tick 又当成"新增"
    // 报一次，列表里就出现两行同一个窗口（窗口集来自 orderedWindowsOf() → listValidWindows(exePath)，
    // 即该进程此刻的全部现存窗口，与 winActiveOrder_ 有没有记录无关）。重播把这段最坏一个 tick 的
    // 时间差压到微秒级；残余的缝隙在另一侧 —— 播种比上面那份列表快照晚一瞬，那一瞬里新建的窗口
    // 会进 seed 却不在列表里，于是不会被报出来（漏一行，而不是重复）。顺序与 brief 一致
    // （列表快照在前、播种在后），与 Task 9 在 AppMode 容忍的是同一类微秒级缝隙。
    watcher_->stop();   // 只为对齐定时器相位；start() 自己会无条件重播 seed，删掉本行不改变行为
    if (cfg.getWatchNewWindows()) watcher_->start();
    groupList_->fill(windows);
    groupList_->setCurrentRow(initialRow);
    if (!layoutForCurrentMode()) {
        mode_ = Mode::AppMode;       // 取不到屏幕就退回，不留半吊子状态
        currentGroupExePath_.clear();
        groupList_->clear();
        return false;
    }
    return true;
}

/// 入口 A：Alt+` 独立按下（切换器不可见）——列表用「前台窗口所在进程」的窗口
/// 入口 B：切换器已可见时按 ` —— 列表用「**出发应用**」所在进程的窗口（见函数开头）
bool Widget::requestShowGroup() {
    // 入口 B：AppMode 可见时按 ` —— 用「出发应用」= 切换器弹出那一刻的前台窗口，而不是当前高亮项。
    // 用户要求 Alt+` 恒等于"我现在所在应用（=A）的窗口列表"，与高亮移到哪个应用无关；
    // 不能在**这里**再取 GetForegroundWindow()：此时前台窗口是本 Widget 自己，取它没有意义。
    if (mode_ == Mode::AppMode && this->isVisible()) {
        QString exePath;
        if (foregroundAtShow_) exePath = Util::getWindowProcessPath(foregroundAtShow_);
        // 出发窗口可能在这期间被关掉/被过滤掉，解析不出进程路径 —— 退回"高亮项"那份更老的定义，
        // 保证按了 ` 不会没反应（代价是那时又回到"跟着高亮走"）
        if (exePath.isEmpty()) {
            if (auto* cur = lw->currentItem())
                exePath = cur->data(Qt::UserRole).value<WindowGroup>().exePath;
        }
        if (exePath.isEmpty()) return false;
        // 初始高亮第 0 行 = 该组最后使用的窗口（按活跃度降序的首行，见 orderedGroupWindows）。
        // 不变式（**当该组最近使用的窗口此刻仍是有效窗口时**成立）：按 ` 再松手 == 什么都不做的松手。
        // 换用出发应用之后它依然成立：你刚从那个窗口过来 ⇒ 它就是该组的 MRU ⇒ 第 0 行就是它。
        // 该条件不成立时（MRU 窗口已被关闭/被过滤，或列表构建后窗口集有变），AppMode 的释放回退
        // 会去读未排序的旧快照（见 widget.cpp:251 的既有 TODO），此时本入口反而更准。
        // 必须用返回值：丢掉它会让"陈旧 GroupMode"那一类 bug 复活（Task 7 的修复轮刚堵掉）
        if (!enterGroupMode(exePath, 0)) return false;
        return this->isVisible();
    }

    pinnedScreen_ = nullptr;   // 入口 A：新的一次弹出，重新按配置选屏（同上）
    auto foreWin = GetForegroundWindow();
    auto exePath = Util::getWindowProcessPath(foreWin);
    if (exePath.isEmpty()) return false;

    auto order = winActiveOrder_.orderedWindowsOf(exePath);
    // 多屏选项 1：与 enterGroupMode()/orderedGroupWindows() 用同一块屏幕过滤；
    // 否则当前屏幕与前台窗口不在同一屏时，初始高亮会按一个列表里根本不存在的下标算（用户 2026-10-18 的决策）
    if (cfg.getMonitorSwitchMode() == MonitorSwitchMode::CurrentOnly) {
        if (const HMONITOR targetMonitor = Util::screenHandle(targetScreen())) {
            QList<HWND> filtered;
            for (auto hwnd: order)
                if (Util::monitorOfWindow(hwnd) == targetMonitor) filtered << hwnd;
            order = filtered;
        }
    }
    if (order.isEmpty()) return false;

    // 初始高亮 = 旧盲切的目标：前台窗口下标 +1（Shift 则 -1），端点循环。
    // 用下标算，不假设"前台窗口一定排首行"——那样高亮会偏。
    // 前台窗口不在过滤后的列表里（跨屏）时落到第 0 行，而不是把不存在的下标当 0 再 +1
    const int n = order.size();
    const int idx = order.indexOf(foreWin);
    const bool backward = Util::isKeyPressed(VK_SHIFT);
    const int delta = backward ? -1 : 1;
    const int initialRow = idx >= 0 ? ((idx + delta) % n + n) % n : 0;

    // 按 enterGroupMode() 的**返回值**判断，不能用 mode_ 的旧值：
    // 调用方原本可能就在 GroupMode（列表已隐藏），此时提前返回会让 mode_ 仍是 GroupMode，
    // 于是 forceShow() 会把上一条遗留的列表显示出来（且松手还会提交那行）
    if (!enterGroupMode(exePath, initialRow)) return false;

    // 几何算完还得真正把窗口显示出来：情形 A 的切换器原本是隐藏的，
    // 沿用 AppMode 那套防闪烁的 forceShow()（见设计文档 §2.4）。
    if (!this->isVisible())
        forceShow();
    // 入口 A 的 layoutForCurrentMode() 在 forceShow() **之前**就调度了 60ms 缩略图定时器；
    // 首次 show 可能超过 60ms（main.cpp 注释里记过 472ms），定时器到点时窗口还没可见，
    // updateThumbnail() 会把这次更新当成「不在 GroupMode/不可见」而 hideThumbnail() 掉，
    // 于是缩略图要等用户第一次换高亮才出现。show 之后重新调度一次，保证进入即显示。
    if (this->isVisible())
        scheduleThumbnailUpdate();
    return this->isVisible();
}

/// Alt 抬起时，GroupMode 的提交动作
void Widget::commitGroupSelection() {
    if (auto hwnd = groupList_->currentHwnd()) {
        // 与 AppMode 松手分支一致：AllMoveToCurrent 时先搬到当前屏幕
        if (cfg.getMonitorSwitchMode() == MonitorSwitchMode::AllMoveToCurrent) {
            if (auto* screen = targetScreen())
                Util::moveWindowToMonitor(hwnd, Util::screenHandle(screen));
        }
        Diag::log("#SWITCH", hwnd);
        Util::switchToWindow(hwnd);
        qInfo() << "(Alt+`)Switch to" << Util::getWindowTitle(hwnd) << Util::getClassName(hwnd);
    }
}

/// 从组列表回到应用切换器：GroupMode 下按 Tab 或 ` 都走这里。
/// 高亮停在「组列表所属的那个应用」上 —— 那正是你进组列表前所在的位置，
/// 于是"按 Tab/` 再松手 == 原地不动"（AppMode 松手会切到该组的 MRU 窗口，而它在第 0 行），
/// 与入口 B 的"按 ` 再松手 == 什么都不做"成对。
/// 找不到对应项（该应用的窗口在此期间全关掉了）就保持原来的高亮不动。
void Widget::exitGroupModeToApp() {
    const QString exePath = currentGroupExePath_;

    mode_ = Mode::AppMode;
    currentGroupExePath_.clear();
    userNavigated_ = false;   // 新的一屏交互，自动选中可以重新接管（与 requestShow() 一致）
    groupList_->clear();      // 内容下次进入时会重新填充

    if (!exePath.isEmpty()) {
        for (int i = 0; i < lw->count(); ++i) {
            if (lw->item(i)->data(Qt::UserRole).value<WindowGroup>().exePath == exePath) {
                lw->setCurrentRow(i);
                break;
            }
        }
    }
    // 回到 AppMode 的几何。失败（取不到屏幕）时不动 mode_：与 enterGroupMode() 的收尾不同，
    // 这里退回 GroupMode 反而会留下"隐藏着的组列表"，不如就地停在 AppMode（列表仍是可见的）
    if (!layoutForCurrentMode())
        qWarning() << "exitGroupModeToApp: layoutForCurrentMode failed";
    showLabelForItem(lw->currentItem());
}

bool Widget::eventFilter(QObject* watched, QEvent* event) {
    // 真实事件的落点是 viewport 而非滚动区对象本身，所以两个分支都同时匹配"对象"与其 viewport。
    // 对象那一半不多余：`:151` 的 ↑↓ 转发是 `postEvent(lw, …)`，直接投给对象（实测 watched == lw），旧注释的"滚轮冒泡"说法不成立。
    const bool onAppList = (watched == lw || watched == lw->viewport());
    const bool onGroupList = groupList_ && (watched == groupList_->widget() || watched == groupList_->widget()->viewport());

    if ((onAppList || onGroupList) && event->type() == QEvent::MouseButtonPress) {
        // 鼠标点选同样是"用户手动移动选中"。app 列表的 item 是可点选的（`:370` 那行
        // `item->setFlags(... & ~Qt::ItemIsSelectable)` 被注释掉了），点一下就会改 currentItem；
        // 组列表同理，点一下会改 currentRow，而动态入列路径在 `:534` 读的就是这个标志。两条都不
        // 经过 keyPressEvent 的任何分支 —— 漏一个，开关 B 打开时选中项就会被新窗口从刚点过的那一项手里抢走
        userNavigated_ = true;
        // 不消费事件：选择行为仍由各自的视图自己处理（重复触发也无害，本分支是幂等的）
    }
    if (mode_ == Mode::GroupMode && onGroupList && event->type() == QEvent::Wheel) {
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
            // 滚轮（以及转发成滚轮的 ↑↓ / Vim K J）同样属于"用户手动移动选中"
            userNavigated_ = true;
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
                    Diag::log("#SWITCH", hwnd);
                    Util::switchToWindow(hwnd, true); // TODO thumbnail隐藏之前 不要switch，并且block滚轮 防止闪烁卡顿
                });
            } else {
                Diag::log("#SWITCH", hwnd);
                Util::switchToWindow(hwnd, true);
            }

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
                            if (HWND taskbar = FindWindow(L"Shell_TrayWnd", nullptr)) {
                                Diag::log("#SWITCH", taskbar);
                                Util::switchToWindow(taskbar, true);
                            }
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
