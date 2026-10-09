#include "utils/TaskbarWheelHooker.h"
#include <QTimer>
#include <QTime>
#include "utils/uiautomation.h"
#include "utils/AppUtil.h"
#include "utils/Util.h"

/// 低级钩子回调：只做廉价判定与事件缓存，重活（UIA查询/路径解析/切窗）全部由主线程
/// processWheelEvent() 异步处理——WH_MOUSE_LL 回调超时（默认1s）会被系统静默跳过，
/// 表现为滚轮偶发失灵，因此回调内不做任何耗时操作。
LRESULT mouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_MOUSEWHEEL) {
        auto* data = (MSLLHOOKSTRUCT*) lParam;
        HWND topLevelHwnd = Util::topWindowFromPoint(data->pt);
        if (Util::isTaskbarWindow(topLevelHwnd)) {
            auto delta = (short) HIWORD(data->mouseData);
            // 仅记录事件，QueuedConnection 交给 GUI 线程处理（instance 生命周期与进程一致，判空兜底）
            if (TaskbarWheelHooker::instance)
                emit TaskbarWheelHooker::instance->rawWheelEvent(delta > 0, topLevelHwnd);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

/// 在 GUI 线程执行原先钩子回调内的全部耗时逻辑
void TaskbarWheelHooker::processWheelEvent(bool isUp, HWND topLevelHwnd) {
    auto delta = isUp ? 120 : -120;
    qDebug() << "--- Taskbar Mouse Wheel" << (delta > 0 ? "↑" : "↓");
    auto el = UIAutomation::getElementUnderMouse(); // RVO优化 不会调用移动构造; // this line may bomb TODO 也许可以改成遍历元素
    qDebug() << delta << el.getClassName() << el.getAutomationId() << el.getName();
    if (el.getClassName() == "CEF-OSC-WIDGET") { // Nvidia Overlay
        // 当所有窗口最小化后，会出现这种情况，但是焦点和前台都不是他，离谱
        qDebug() << "detect CEF, try active taskbar";
        Util::switchToWindow(topLevelHwnd, true); // 只能通过变焦到Taskbar使Element正常检测
        el = std::move(UIAutomation::getElementUnderMouse());
        qDebug() << (el.getClassName() != "CEF-OSC-WIDGET" ? "successful!" : "failed");
    }
    if (el.getClassName() == "Taskbar.TaskListButtonAutomationPeer") {
        auto appid = el.getAutomationId().mid(QStringLiteral("Appid: ").size()); // TODO 在副屏 有时候会是"TaskbarFrame"
        auto name = el.getName();
        int windows = 0;
        const auto Dash = QStringLiteral(" - ");
        if (auto dashIdx = name.lastIndexOf(Dash); dashIdx != -1) { // "Clash for Windows - 1 个运行窗口"
            std::stringstream ss(name.mid(dashIdx + Dash.size()).toStdString());
            ss >> windows; // 从标题手动解析真实窗口数量，程序内部由于过滤逻辑的存在，可能不准确
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

    // 原生钩子线程的 rawWheelEvent → GUI 线程的 processWheelEvent（跨线程必须 QueuedConnection）
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
                // 依赖事件循环
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
