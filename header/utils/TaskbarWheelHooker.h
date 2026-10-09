#ifndef WIN_SWITCHER_TASKBARWHEELHOOKER_H
#define WIN_SWITCHER_TASKBARWHEELHOOKER_H

#include <QObject>
#include <Windows.h>

class TaskbarWheelHooker : public QObject {
    Q_OBJECT

public:
    TaskbarWheelHooker();
    ~TaskbarWheelHooker() override;
    inline static TaskbarWheelHooker* instance = nullptr;

signals:
    /// 钩子线程发出（仅缓存事件，不含耗时逻辑），由本对象以 QueuedConnection 转入 GUI 线程
    void rawWheelEvent(bool isUp, HWND topLevelHwnd);
    void tabWheelEvent(const QString& exePath, bool isUp, int windows); // 参数为引用问题也不大，貌似会自动拷贝（Qt::QueuedConnection情况下）
    void leaveTaskbar(); // 鼠标离开taskbar

private slots:
    /// 在 GUI 线程执行原先钩子回调内的耗时逻辑（UIA查询/路径解析）
    void processWheelEvent(bool isUp, HWND topLevelHwnd);

private:
    HHOOK h_mouse = nullptr;
};


#endif //WIN_SWITCHER_TASKBARWHEELHOOKER_H