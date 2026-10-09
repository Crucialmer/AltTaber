#ifndef WIN_SWITCHER_UTIL_H
#define WIN_SWITCHER_UTIL_H

#include <Windows.h>
#include <QString>
#include <QIcon>
#include <dwmapi.h>
#include <QElapsedTimer>

namespace Util {
    QString getWindowTitle(HWND hwnd);
    QString getClassName(HWND hwnd);
    bool isWindowElevated(HWND hwnd);
    QString getWindowProcessPath(HWND hwnd);
    QList<QString> getChildProcessPaths(const QString& exePath);
    QString getFileDescription(const QString& path);
    bool isTopMost(HWND hwnd);
    void switchToWindow(HWND hwnd, bool force = false);
    void bringWindowToTop(HWND hwnd, HWND hWndInsertAfter = HWND_TOPMOST);
    bool isWindowAcceptable(HWND hwnd, bool skipVisibleCheck = false);
    QList<HWND> enumWindows();
    QList<HWND> enumChildWindows(HWND hwnd);
    QList<HWND> listValidWindows();
    QList<HWND> listValidWindows(const QString& exePath);
    QList<HWND> findTopWindows(const QString& className, const QString& title = QString());
    QIcon getJumboIcon(const QString& filePath);
    QIcon getCachedIcon(const QString& path, HWND hwnd);
    void startIconPrefetch(); // 后台图标预取：工作线程提前提取图标，避免弹窗时首次同步等待
    /// 布防“双击屏蔽”：msecs 内吞掉与当前光标位置（物理坐标，±6px）同点的下一次鼠标左键按下/释放。
    /// 用于“单击即切换”后，废掉双击的第二下，避免其落到切换后的窗口上造成误触
    void armClickShield(int msecs);
    QPixmap getWindowIcon(HWND hwnd);
    bool setWindowRoundCorner(HWND hwnd, DWM_WINDOW_CORNER_PREFERENCE pvAttribute = DWMWCP_ROUND);
    bool isKeyPressed(int vkey);
    QIcon overlayIcon(const QPixmap& icon, const QPixmap& overlay, const QRect& overlayRect);
    HWND topWindowFromPoint(const POINT& pos);
    POINT getCursorPos();
    HWND getCurrentTaskListThumbnailWnd();
    bool isTaskbarWindow(HWND hwnd);
} // Util

#endif //WIN_SWITCHER_UTIL_H
