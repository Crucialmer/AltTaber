#ifndef WIN_SWITCHER_CONFIGMANAGER_H
#define WIN_SWITCHER_CONFIGMANAGER_H

#include <QSettings>
#include <QApplication>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QDir>
#include "ConfigManagerBase.h"

// 注意：对于大量使用的类，header-only 模式会导致编译时间过长
#define cfg ConfigManager::instance()

enum DisplayMonitor {
    PrimaryMonitor, // 0 主显示器
    MouseMonitor, // 1 跟随鼠标
    EnumCount // Just for count
};

class ConfigManager : public ConfigManagerBase {
    inline static const QString FileName = "config.ini";

public:
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    static ConfigManager& instance() {
        static const auto filePath = [] {
            // 注意：QFileInfo::isWritable() 在 Windows 上默认只检查 READONLY 属性（NTFS ACL 检查需
            // 启用 qt_ntfs_permission_lookup，默认关闭），对 Program Files 会误报可写。
            // 因此这里用"实测写临时文件"来判断目录是否真的可写。
            const auto appDir = QApplication::applicationDirPath();
            QTemporaryFile probe(appDir + "/.writable_probe_XXXXXX.tmp");
            const bool writable = probe.open();
            probe.remove(); // QTemporaryFile 析构会自动删除，这里显式删除以保险
            if (!writable) {
                qWarning() << "Application dir not writable, config fallback to AppData";
                const auto appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                QDir().mkpath(appData);
                return appData + "/" + FileName;
            }
            return appDir + "/" + FileName;
        }();
        static ConfigManager instance{filePath}; // multiple threads safe
        return instance;
    }

public:
    DisplayMonitor getDisplayMonitor() {
        auto monitor = get("DisplayMonitor", DisplayMonitor::MouseMonitor).toInt();
        if (monitor < 0 || monitor >= DisplayMonitor::EnumCount) {
            qWarning() << "Invalid DisplayMonitor enum" << monitor;
            monitor = DisplayMonitor::MouseMonitor;
        }
        return static_cast<DisplayMonitor>(monitor);
    }

    void setDisplayMonitor(DisplayMonitor monitor) {
        set("DisplayMonitor", monitor);
    }

private:
    explicit ConfigManager(const QString& filename) : ConfigManagerBase(filename) {}
};


#endif //WIN_SWITCHER_CONFIGMANAGER_H
