#ifndef WIN_SWITCHER_WIDGET_H
#define WIN_SWITCHER_WIDGET_H

#include <QWidget>
#include <Windows.h>
#include <QListWidget>
#include <QDebug>
#include "utils/WindowActivationOrder.h"
#include "utils/WindowInfo.h"
#include "utils/GroupWindowList.h"

struct WindowGroup;

struct WindowGroup {
    WindowGroup() = default;

    void addWindow(const WindowInfo& window) {
        windows.append(window);
    }

    QString exePath;
    QIcon icon;
    QList<WindowInfo> windows;
};

Q_DECLARE_METATYPE(WindowGroup) // for QVariant

QT_BEGIN_NAMESPACE

namespace Ui {
    class Widget;
}

QT_END_NAMESPACE

class Widget : public QWidget {
    Q_OBJECT

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

public:
    enum ForegroundChangeSource {
        WinEvent,
        Inner,
    };

    Q_ENUM(ForegroundChangeSource) // for QMetaEnum, to QString

public:
    enum class Mode { AppMode, GroupMode };

public:
    explicit Widget(QWidget* parent = nullptr);
    QList<WindowGroup> prepareWindowGroupList();
    bool prepareListWidget();
    Q_INVOKABLE bool requestShow();
    Q_INVOKABLE bool requestShowGroup();
    void notifyForegroundChanged(HWND hwnd, ForegroundChangeSource source);

    HWND hWnd() { return (HWND) winId(); }

    bool isForeground() { return GetForegroundWindow() == hWnd(); }

    ~Widget() override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void rotateTaskbarWindowInGroup(const QString& exePath, bool forward, int windows);
    void clearGroupWindowOrder();

private:
    bool forceShow();
    void showLabelForItem(QListWidgetItem* item, QString text = QString());
    void setupLabelFont();
    static HWND rotateWindowInGroup(const QList<HWND>& windows, HWND current, bool forward = true);
    static HWND rotateNormalWindowInGroup(const QList<HWND>& windows, HWND current, bool forward = true);

    /// 进入 GroupMode：用给定进程的窗口填充竖排列表，并定好初始高亮。
    /// 返回 false 表示没有进入（exePath 为空 / 没有窗口 / 取不到屏幕），
    /// 调用方必须按返回值判断，不能拿 mode_ 的旧值当"成功"的信号
    bool enterGroupMode(const QString& exePath, int initialRow);
    /// Alt 抬起时，GroupMode 的提交动作
    void commitGroupSelection();
    /// 本程序**唯一**的窗口几何计算处：按当前 mode_ 决定内容尺寸，
    /// 再把窗口居中到目标屏幕。返回 false 表示取不到目标屏幕
    bool layoutForCurrentMode();
    /// 按 DisplayMonitor 配置挑选目标屏幕（失败回退主屏）。原本内联在 prepareListWidget() 里，
    /// 抽出来给两个模式共用，避免复制一份屏幕选择逻辑
    QScreen* targetScreen();
    /// 该进程的窗口，按最后使用时间降序
    QList<WindowInfo> orderedGroupWindows(const QString& exePath);

private:
    Ui::Widget* ui;
    QListWidget* lw = nullptr;
    const QMargins ListWidgetMargin{24, 24, 24, 24};
    WindowActivationOrder winActiveOrder_;
    QList<HWND> groupWindowOrder; // for Alt+` 同组窗口切换
    Mode mode_ = Mode::AppMode;
    GroupWindowList* groupList_ = nullptr;   // 在构造函数里 new，parent 为 this
    bool userNavigated_ = false;             // 本次弹出期间用户是否手动翻过列表
};


#endif //WIN_SWITCHER_WIDGET_H
