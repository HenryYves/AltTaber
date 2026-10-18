#ifndef WIN_SWITCHER_WIDGET_H
#define WIN_SWITCHER_WIDGET_H

#include <QWidget>
#include <Windows.h>
#include <QListWidget>
#include <QDebug>
#include <QPointer>
#include <QImage>
#include <QColor>
#include "utils/WindowActivationOrder.h"
#include "utils/WindowInfo.h"
#include "utils/GroupWindowList.h"
#include "utils/WindowWatcher.h"
#include "utils/DwmThumbnail.h"
#include "utils/ConfigManager.h"

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

class QTimer;

class Widget : public QWidget {
    Q_OBJECT

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;

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

    /// 组列表的字号/字体（[group_list]）：构造时应用一次，配置被编辑后重新应用
    void setupGroupListFont();

    /// 背景模式（[appearance]）：解析 auto/system/self/tint，并在需要时截取弹窗背后的画面。
    /// 必须在弹窗 show **之前**调用（自绘模式要截到未被弹窗遮挡的桌面）。
    void prepareBackground();
    /// 按 effectiveBackground_ 开/关 DWM accent blur。show 之后还要再调一次：
    /// forceShow() 的 showMinimized/showNormal 可能重置窗口合成状态
    void applyBackgroundAccent();
    /// 截取弹窗当前矩形背后的屏幕内容，按 blur_scale 缩放、按 blur_radius 模糊后存起来
    void captureBackgroundSnapshot();
    /// 系统「透明效果」开关（HKCU\...\Themes\Personalize\EnableTransparency）
    static bool transparencyEffectsEnabled();
    /// 从组列表回到应用切换器（GroupMode 下 Tab 与 ` 都走这里）。
    /// 高亮停在「组列表所属的那个应用」上，于是"按 Tab/` 再松手 == 原地不动"，
    /// 与入口 B 的"按 ` 再松手 == 什么都不做"成对
    void exitGroupModeToApp();

    /// 缩略图宿主与 DWM 封装：构造时创建一次。宿主是独立顶层窗口
    /// （Qt::Tool + 无边框 + 置顶 + 不抢焦点 + 输入穿透 + 半透明背景），
    /// 因为 DWM 的窗口圆角只能作用于整个顶层窗口，主弹窗无法只圆缩略图那一块
    void setupThumbnail();
    /// 高亮/内容变化后重置 60ms 单次定时器：只处理最后一次，不阻塞按键路径
    void scheduleThumbnailUpdate();
    /// 定时器到点：按当前高亮行注册/更新缩略图并定位宿主；不满足条件则隐藏
    void updateThumbnail();
    /// 停掉待处理定时器并隐藏缩略图与宿主（保留 DWM 注册，下次可直接复用）
    void hideThumbnail();
    /// 宿主圆角：优先 DWMWCP_ROUND，失败（Win10）回退 SetWindowRgn 圆角区域
    void applyThumbnailHostRegion();

    /// `lw` 的宽度公式（项数 × 栅格宽 + 修正）。只此一处：
    /// 列表是 IconMode + 不换行 + 无滚动条，宽度必须正好等于内容宽，
    /// 所以 prepareListWidget() 与动态入列/移除三条路径都要调它
    void updateListWidth();
    /// 缩略图预留盒的实际尺寸（逻辑像素）：配置值再被屏幕可用区域兜一次，
    /// 保证「列表 + 间距 + 缩略图」不会超出屏幕（layout 与定时器共用同一份）
    QSize thumbnailBoxSize();
    /// 轮询发现的新窗口：AppMode 下并入同进程的组，或为新进程插入一项
    void onNewWindows(const QList<HWND>& added);
    /// 轮询发现的已消失窗口：移除窗口已全部关闭的组
    void onWindowsGone(const QList<HWND>& removed);
    /// AppMode：为新进程插入一个列表项（沿用既有排序语义：无使用记录 → 末尾）
    int insertAppItem(const QString& exePath, HWND hwnd);
    /// 移除窗口已全部关闭的组。返回是否真的删掉了项（项数没变就别动几何）
    bool removeEmptyGroups();

private:
    Ui::Widget* ui;
    QListWidget* lw = nullptr;
    const QMargins ListWidgetMargin{24, 24, 24, 24};
    WindowActivationOrder winActiveOrder_;
    QList<HWND> groupWindowOrder; // for Alt+` 同组窗口切换
    /// GroupMode 期间"本组"的进程路径：动态扫描靠它过滤掉别的进程的新窗口。
    /// 在 enterGroupMode() 里随 mode_ 一起设置，onNewWindows() 只读
    QString currentGroupExePath_;
    /// 切换器弹出那一刻的前台窗口 = 入口 B 的「出发应用」。**只在本窗口还没显示时记录**：
    /// requestShow() 会被 main.cpp 的重试循环与重复按 Alt+Tab 调用，那时前台已经是本窗口自己，
    /// GetForegroundWindow() 取到的没有意义（入口 B 原来就是因为这点才改用高亮项）
    HWND foregroundAtShow_ = nullptr;
    Mode mode_ = Mode::AppMode;
    GroupWindowList* groupList_ = nullptr;   // 在构造函数里 new，parent 为 this
    WindowWatcher* watcher_ = nullptr;       // 同上；只在列表可见时运行
    /// 组列表右侧的 DWM 缩略图（[group_list] thumbnail=true 时生效）：
    /// host 是独立顶层窗口（parent 为 Widget，即 owner），缩略图由 DWM 合成到它上面。
    /// 宿主尺寸/位置一律用物理像素经 SetWindowPos 设置（见 f3）。
    QWidget* thumbnailHost_ = nullptr;
    DwmThumbnail* thumbnail_ = nullptr;      // 析构时显式 delete：必须先注销再让 Qt 销毁宿主
    QTimer* thumbnailTimer_ = nullptr;       // 60ms 单次合并定时器
    static constexpr int ThumbnailGap = 12;  // 列表与缩略图之间的间距（逻辑像素）
    /// 宿主圆角是否走通了 Win11 的原生 API；没走通则每次改尺寸后重设圆角区域
    bool thumbnailNativeCorner_ = false;
    bool thumbnailCornerTried_ = false;
    /// 本次弹出钉住的屏幕（见 targetScreen()）：动态入列会重跑几何，
    /// 若每次都按光标重新选屏，跟随鼠标模式下弹窗会在中途跳到另一个显示器。
    /// 每次弹出开始时清空、hideEvent 再清一次。
    /// 用 QPointer：QScreen 归 QGuiApplication 所有，显示器被拔掉/拓扑变化时会**立即析构**，
    /// 裸指针会悬空 —— 而 hideEvent 并不构成寿命上界（弹窗可以一直开着）
    QPointer<QScreen> pinnedScreen_;
    bool userNavigated_ = false;             // 本次弹出期间用户是否手动翻过列表
    /// 本次弹出的实际背景（auto 已解析成 system 或 self）
    BackgroundMode effectiveBackground_ = BackgroundSystem;
    /// 自绘模糊后的弹窗背景（只存降采样后的小图，不含系统 blur）
    QImage backgroundSnapshot_;
    /// 截图对应的全局逻辑矩形。paint 时按它在屏幕上的原位置绘制，不随弹窗尺寸拉伸
    /// （入口 B 从 AppMode 切到 GroupMode 时弹窗会变大，拉伸会把背景放大）
    QRect backgroundSnapshotGlobal_;
    /// 叠加在背景上的颜色（含 alpha），由 [appearance] 解析
    QColor backgroundOverlay_ = QColor(20, 20, 20, 140);
};


#endif //WIN_SWITCHER_WIDGET_H
