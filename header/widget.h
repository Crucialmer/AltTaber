#ifndef WIN_SWITCHER_WIDGET_H
#define WIN_SWITCHER_WIDGET_H

#include <QWidget>
#include <Windows.h>
#include <QListWidget>
#include <QDebug>

struct WindowGroup;

struct WindowInfo {
    QString title;
    QString className;
    HWND hwnd = nullptr;
};

// C++17 inline: 防止重定义
inline QDebug operator<<(QDebug dbg, const WindowInfo& info) {
    dbg.nospace() << "WindowInfo(" << info.title << ", " << info.className << ", " << info.hwnd << ")";
    return dbg.space();
}

Q_DECLARE_METATYPE(WindowInfo) // for QVariant
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
    explicit Widget(QWidget* parent = nullptr);
    QList<WindowGroup> prepareWindowGroupList();
    bool prepareListWidget();
    Q_INVOKABLE bool requestShow();
    Q_INVOKABLE void requestShowPinned(); // Ctrl+Alt+Tab：松开按键后保持显示，Enter/点击确认，Esc 取消
    void notifyForegroundChanged(HWND hwnd, ForegroundChangeSource source);

    HWND hWnd() { return (HWND) winId(); }

    bool isForeground() { return GetForegroundWindow() == hWnd(); }

    ~Widget() override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void rotateTaskbarWindowInGroup(const QString& exePath, bool forward, int windows);
    void clearGroupWindowOrder();

private:
    bool forceShow();
    /// 执行"切换到当前选中项"并隐藏（Alt释放与pinned确认共用）
    void switchToCurrentItem();
    void showLabelForItem(QListWidgetItem* item, QString text = QString());
    void setupLabelFont();
    auto getLastActiveGroupWindow(const QString& exePath) -> QPair<HWND, QDateTime>;
    auto getLastValidActiveGroupWindow(const WindowGroup& group) -> QPair<HWND, QDateTime>;
    void sortGroupWindows(QList<HWND>& windows, const QString& exePath);
    QList<HWND> buildGroupWindowOrder(const QString& exePath);
    static HWND rotateWindowInGroup(const QList<HWND>& windows, HWND current, bool forward = true);
    static HWND rotateNormalWindowInGroup(const QList<HWND>& windows, HWND current, bool forward = true);

private:
    Ui::Widget* ui;
    QListWidget* lw = nullptr;
    const QMargins ListWidgetMargin{24, 24, 24, 24};
    /// exePath -> (HWND, time)
    QHash<QString, QHash<HWND, QDateTime>> winActiveOrder;
    QList<HWND> groupWindowOrder; // for Alt+` 同组窗口切换

    // --- 状态：替代原函数内 static（跨调用存活、存在悬垂/污染风险）---
    // eventFilter 滚轮换窗状态
    QListWidgetItem* wheelLastItem = nullptr;
    HWND wheelHwnd = nullptr;
    bool isLastWheelRollUp = true;
    // rotateTaskbarWindowInGroup 状态
    QString taskbarLastPath;
    HWND taskbarLastHwnd = nullptr;
    bool isLastTaskbarForward = true;
    // Ctrl+Alt+Tab pinned 模式：松开按键后保持显示，Enter/点击确认，Esc 取消
    bool pinned = false;
};


#endif //WIN_SWITCHER_WIDGET_H
