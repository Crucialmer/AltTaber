ï»¿#include "utils/TaskbarWheelHooker.h"
#include <QTimer>
#include <QTime>
#include "utils/uiautomation.h"
#include "utils/AppUtil.h"
#include "utils/Util.h"

/// ä½çº§é©å­åè°ï¼åªåå»ä»·å¤å®ä¸äºä»¶ç¼å­ï¼éæ´»ï¼UIAæ¥è¯¢/è·¯å¾è§£æ/åçªï¼å¨é¨ç±ä¸»çº¿ç¨
/// processWheelEvent() å¼æ­¥å¤çââWH_MOUSE_LL åè°è¶æ¶ï¼é»è®¤1sï¼ä¼è¢«ç³»ç»éé»è·³è¿ï¼
/// è¡¨ç°ä¸ºæ»è½®å¶åå¤±çµï¼å æ­¤åè°åä¸åä»»ä½èæ¶æä½ã
LRESULT mouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_MOUSEWHEEL) {
        auto* data = (MSLLHOOKSTRUCT*) lParam;
        HWND topLevelHwnd = Util::topWindowFromPoint(data->pt);
        if (Util::isTaskbarWindow(topLevelHwnd)) {
            auto delta = (short) HIWORD(data->mouseData);
            // ä»è®°å½äºä»¶ï¼QueuedConnection äº¤ç» GUI çº¿ç¨å¤çï¼instance çå½å¨æä¸è¿ç¨ä¸è´ï¼å¤ç©ºååºï¼
            if (TaskbarWheelHooker::instance)
                emit TaskbarWheelHooker::instance->rawWheelEvent(delta > 0, topLevelHwnd);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

/// å¨ GUI çº¿ç¨æ§è¡ååé©å­åè°åçå¨é¨èæ¶é»è¾
void TaskbarWheelHooker::processWheelEvent(bool isUp, HWND topLevelHwnd) {
    auto delta = isUp ? 120 : -120;
    qDebug() << "--- Taskbar Mouse Wheel" << (delta > 0 ? "â" : "â");
    auto el = UIAutomation::getElementUnderMouse(); // RVOä¼å ä¸ä¼è°ç¨ç§»å¨æé ; // this line may bomb TODO ä¹è®¸å¯ä»¥æ¹æéååç´ 
    qDebug() << delta << el.getClassName() << el.getAutomationId() << el.getName();
    if (el.getClassName() == "CEF-OSC-WIDGET") { // Nvidia Overlay
        // å½ææçªå£æå°ååï¼ä¼åºç°è¿ç§æåµï¼ä½æ¯ç¦ç¹ååå°é½ä¸æ¯ä»ï¼ç¦»è°±
        qDebug() << "detect CEF, try active taskbar";
        Util::switchToWindow(topLevelHwnd, true); // åªè½éè¿åç¦å°Taskbarä½¿Elementæ­£å¸¸æ£æµ
        el = std::move(UIAutomation::getElementUnderMouse());
        qDebug() << (el.getClassName() != "CEF-OSC-WIDGET" ? "successful!" : "failed");
    }
    if (el.getClassName() == "Taskbar.TaskListButtonAutomationPeer") {
        auto appid = el.getAutomationId().mid(QStringLiteral("Appid: ").size()); // TODO å¨å¯å± ææ¶åä¼æ¯"TaskbarFrame"
        auto name = el.getName();
        int windows = 0;
        const auto Dash = QStringLiteral(" - ");
        if (auto dashIdx = name.lastIndexOf(Dash); dashIdx != -1) { // "Clash for Windows - 1 ä¸ªè¿è¡çªå£"
            std::stringstream ss(name.mid(dashIdx + Dash.size()).toStdString());
            ss >> windows; // ä»æ é¢æå¨è§£æçå®çªå£æ°éï¼ç¨åºåé¨ç±äºè¿æ»¤é»è¾çå­å¨ï¼å¯è½ä¸åç¡®
            name = name.left(dashIdx);
        }
        auto exePath = AppUtil::getExePathFromAppIdOrName(appid, name);
        emit tabWheelEvent(exePath, isUp, windows);
    }
}

TaskbarWheelHooker::TaskbarWheelHooker() {
    if (instance) {
        qCritical() << "Only one TaskbarWheelHooker can be installed!";
        return;
    }
    instance = this;
    AppUtil::getExePathFromAppIdOrName(); // cache

    // åçé©å­çº¿ç¨ç rawWheelEvent â GUI çº¿ç¨ç processWheelEventï¼è·¨çº¿ç¨å¿é¡» QueuedConnectionï¼
    connect(this, &TaskbarWheelHooker::rawWheelEvent, this, &TaskbarWheelHooker::processWheelEvent,
            Qt::QueuedConnection);

    auto* timer = new QTimer(this);
    timer->callOnTimeout(this, [this]() {
        static bool isLastTaskbar = false;
        HWND topLevelHwnd = Util::topWindowFromPoint(Util::getCursorPos());
        bool isTaskbar = Util::isTaskbarWindow(topLevelHwnd);
        if (isLastTaskbar != isTaskbar) {
            isLastTaskbar = isTaskbar;
            if (isTaskbar) {
                // ä¾èµäºä»¶å¾ªç¯
                h_mouse = SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC) mouseProc, GetModuleHandle(nullptr), 0);
                if (h_mouse == nullptr)
                    qCritical() << "Failed to install h_mouse";
                qDebug() << "#Enter Taskbar" << QTime::currentTime();
            } else {
                UnhookWindowsHookEx(h_mouse);
                h_mouse = nullptr;
                qDebug() << "#Leave Taskbar" << QTime::currentTime();
                emit leaveTaskbar();
            }
        }
    });
    timer->start(50);
}

TaskbarWheelHooker::~TaskbarWheelHooker() {
    if (h_mouse) {
        UnhookWindowsHookEx(h_mouse);
        qDebug() << "MouseHooker uninstalled";
    }
    TaskbarWheelHooker::instance = nullptr;
    UIAutomation::cleanup();
}
