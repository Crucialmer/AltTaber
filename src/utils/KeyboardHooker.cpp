#include "utils/KeyboardHooker.h"
#include <QDebug>
#include <QApplication>
#include <QKeyEvent>
#include "utils/Util.h"
#include "widget.h"

LRESULT keyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    using Hooker = KeyboardHooker;
    // Tab 自动重复守卫：按住按键时系统约30ms触发一次重复 keydown，pinned 模式下重复的
    // requestShowPinned 会被误判为"再次按下=确认切换"，导致弹出窗口刚显示就被切走（无法保持）。
    // 仅对 Ctrl+Alt+Tab 呼出去重；普通 Alt+Tab 的重复仍放行（按住 Tab 连续切换是原设计行为）。
    static bool s_ctrlTabDown = false;
    if (nCode == HC_ACTION) {
        if (wParam == WM_SYSKEYDOWN || wParam == WM_KEYDOWN) { // Alt & [Alt按下时的Tab]属于SysKey
            auto* pKeyBoard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            // inner: `GetAsyncKeyState`, doc warns this usage, but it seems to work fine(?)
            // If it's broken, maybe we can record Modifier manually in every callback
            /* Note from Docs:
             * When this callback function is called in response to a change in the state of a key,
             * the callback function is called before the asynchronous state of the key is updated.
             * Consequently, the asynchronous state of the key cannot be determined by calling GetAsyncKeyState from within the callback function.
             * */
            bool isAltPressed = Util::isKeyPressed(VK_MENU);

            if (isAltPressed && Hooker::receiver) {
                if (pKeyBoard->vkCode == VK_TAB) {
                    bool isCtrlPressed = Util::isKeyPressed(VK_CONTROL);
                    if (isCtrlPressed) {
                        if (s_ctrlTabDown)
                            return 1; // 自动重复事件，忽略（只处理首次按下）
                        s_ctrlTabDown = true;
                        // Ctrl+Alt+Tab：松手后切换器保持显示（pinned），Enter/点击确认，Esc 取消
                        qDebug() << "Ctrl+Alt+Tab detected!";
                        QMetaObject::invokeMethod(Hooker::receiver, "requestShowPinned", Qt::QueuedConnection);
                        return 1; // 阻止事件传递（拦截系统的 Ctrl+Alt+Tab 固定切换器）
                    }
                    qDebug() << "Alt+Tab detected!";
                    if (Hooker::receiver->isVisible() && !Hooker::receiver->isMinimized()) {
                        // 弹出器已显示（pinned）：直接转发循环选择——不依赖键盘焦点，
                        // 抢焦点失败（可见但非前台）时也能连续切换，且不刷新列表、不丢选择
                        auto shiftModifier = Util::isKeyPressed(VK_SHIFT) ? Qt::ShiftModifier : Qt::NoModifier;
                        QApplication::postEvent(Hooker::receiver, new QKeyEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::AltModifier | shiftModifier));
                    } else if ((HWND) Hooker::receiver->winId() != GetForegroundWindow()) { // not Foreground
                        // 异步，防止阻塞；超过1s会导致被系统强制绕过，传递给下一个钩子
                        // 方案A：Alt+Tab 也走 pinned 模式——松开 Alt 后切换器保持显示，Enter/空格/点击确认，Esc 取消
                        QMetaObject::invokeMethod(Hooker::receiver, "requestShowPinned", Qt::QueuedConnection);
                    } else {
                        // 转发Alt+Tab给Widget
                        auto shiftModifier = Util::isKeyPressed(VK_SHIFT) ? Qt::ShiftModifier : Qt::NoModifier;
                        auto tabDownEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::AltModifier | shiftModifier);
                        QApplication::postEvent(Hooker::receiver, tabDownEvent); // async
                    }
                    return 1; // 阻止事件传递
                } else if (pKeyBoard->vkCode == VK_OEM_3) { // ~`
                    qDebug() << "Alt+` detected!";
                    auto shiftModifier = Util::isKeyPressed(VK_SHIFT) ? Qt::ShiftModifier : Qt::NoModifier;
                    auto event = new QKeyEvent(QEvent::KeyPress, Qt::Key_QuoteLeft, Qt::AltModifier | shiftModifier);
                    QApplication::postEvent(Hooker::receiver, event); // async
                    return 1; // 阻止事件传递
                }
            }

            // 弹出器显示中（pinned）：接管选择/确认/取消键——不依赖系统键盘焦点。
            // 松开 Alt 后按键若靠系统投递：焦点不在弹窗上时直接丢失、焦点在列表控件上时被
            // 消费（Tab=焦点导航、`=键盘搜索）；Alt 按住时还会泄漏为系统快捷键（Alt+←/→=
            // 前进后退、Alt+Esc=窗口循环）。统一在此截获为合成事件直达 Widget。
            const bool popupActive = Hooker::receiver && Hooker::receiver->isVisible()
                                     && !Hooker::receiver->isMinimized(); // isVisible() 在最小化时仍为 true
            if (popupActive) {
                const bool ctrl = Util::isKeyPressed(VK_CONTROL);
                const bool winPressed = Util::isKeyPressed(VK_LWIN) || Util::isKeyPressed(VK_RWIN);
                const bool isForeground = (HWND) Hooker::receiver->winId() == GetForegroundWindow();
                int qtKey = 0;
                // Win 组合（Win+Tab / Win+方向键 / Win+Space 等）一律放行；
                // Ctrl 组合逐项判断（Ctrl+Space=输入法切换、Ctrl+Esc=开始菜单、Ctrl+Tab 等均放行）
                if (!winPressed) {
                    switch (pKeyBoard->vkCode) {
                        // Alt 按住时 Tab/` 已由上方分支处理
                        case VK_TAB:    if (!isAltPressed && !ctrl) qtKey = Qt::Key_Tab; break;
                        case VK_OEM_3:  if (!isAltPressed && !ctrl) qtKey = Qt::Key_QuoteLeft; break;
                        case VK_RETURN: if (!ctrl) qtKey = Qt::Key_Return; break;
                        case VK_SPACE:  if (!ctrl) qtKey = Qt::Key_Space; break;
                        case VK_ESCAPE: if (!ctrl) qtKey = Qt::Key_Escape; break; // Ctrl+Esc=开始菜单，放行
                        case VK_UP:     if (!ctrl) qtKey = Qt::Key_Up; break;
                        case VK_DOWN:   if (!ctrl) qtKey = Qt::Key_Down; break;
                        case VK_LEFT:   if (!ctrl) qtKey = Qt::Key_Left; break;
                        case VK_RIGHT:  if (!ctrl) qtKey = Qt::Key_Right; break;
                        // Vim 键直接映射为方向键（绕过列表控件原生键盘搜索/事件冒泡的不确定性）；
                        // 仅当弹窗持有前台时接管，避免吞掉在其他窗口正常输入 h/j/k/l 字母。
                        // 注：SDK 未定义字母键 VK_ 宏，字母键 vkCode 即大写 ASCII 码，直接用字符字面量
                        case 'H':      if (isForeground && !ctrl) qtKey = Qt::Key_Left; break;
                        case 'J':      if (isForeground && !ctrl) qtKey = Qt::Key_Down; break;
                        case 'K':      if (isForeground && !ctrl) qtKey = Qt::Key_Up; break;
                        case 'L':      if (isForeground && !ctrl) qtKey = Qt::Key_Right; break;
                        default: break;
                    }
                }
                if (qtKey) {
                    Qt::KeyboardModifiers mods = Util::isKeyPressed(VK_SHIFT) ? Qt::ShiftModifier : Qt::NoModifier;
                    if (qtKey == Qt::Key_Tab)
                        mods |= Qt::AltModifier; // 无修饰键的 Tab 会被 QWidget::event 当焦点切换吞掉，带 Alt 可绕过
                    QApplication::postEvent(Hooker::receiver, new QKeyEvent(QEvent::KeyPress, qtKey, mods));
                    return 1; // 阻止系统投递：按键仅由弹出器处理
                }
            }
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) { // Amazing, Alt Down is `WM_SYSKEYDOWN`, but release is `WM_KEYUP`
            auto* pKeyBoard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            if (pKeyBoard->vkCode == VK_TAB)
                s_ctrlTabDown = false; // 释放时重置自动重复守卫
            if (pKeyBoard->vkCode == VK_LMENU && Hooker::receiver) {
                // BUG: Alt + 方向键 长按，过一秒会触发Alt release，而Alt + 其他键则不会，可能是Windows保护机制或键盘问题？
                qDebug() << "Alt released!";
                auto event = new QKeyEvent(QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
                QApplication::postEvent(Hooker::receiver, event); // async
                // not block
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

KeyboardHooker::KeyboardHooker(QWidget* _receiver) {
    if (KeyboardHooker::receiver) {
        qWarning() << "Only one KeyboardHooker can be installed!";
        return;
    }
    // 回调函数的执行与消息循环密切相关，在Get/PeekMessage时，系统才会触发回调; [https://learn.microsoft.com/en-us/windows/win32/winmsg/mouseproc]
    h_keyboard = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC) keyboardProc, GetModuleHandle(nullptr), 0);
    if (!h_keyboard) {
        qWarning() << "Failed to install h_keyboard!";
        return;
    }
    if (!_receiver) {
        qWarning() << "Receiver is nullptr!";
        return;
    }
    KeyboardHooker::receiver = _receiver;
    qInfo() << "KeyboardHooker installed";
}

KeyboardHooker::~KeyboardHooker() {
    if (!h_keyboard) return;
    UnhookWindowsHookEx(h_keyboard);
    KeyboardHooker::receiver = nullptr;
    qDebug() << "KeyboardHooker uninstalled";
}
