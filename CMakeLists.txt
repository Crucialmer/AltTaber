cmake_minimum_required(VERSION 3.21)
project(Win_Switcher VERSION 0.5.5)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# 不硬编码 Qt 路径，改由命令行 -DCMAKE_PREFIX_PATH= 或环境变量传入
list(APPEND CMAKE_AUTOUIC_SEARCH_PATHS ui) # 如果.ui文件不在.cpp同一目录下，需要添加搜索路径

# 版本信息统一来自 project() 声明（applicationVersion 也由此注入）
set(VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(VERSION_PATCH ${PROJECT_VERSION_PATCH})

set(COMPANY_NAME "MrBeanCpp")
set(FILE_DESCRIPTION "AltTaber - Window Switcher")
set(PRODUCT_NAME "AltTaber")
# 设置图标的相对路径
# 图标会被缓存，修改后不生效的话，需要删除AppData\Local\IconCache.db & 重启资源管理器，该文件在PC重启后自动创建
set(ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/img/icon.ico")

set(VERSIONINFO_RC "${CMAKE_CURRENT_BINARY_DIR}/VersionInfo.rc")
# 处理.in模板文件 生成版本信息文件
configure_file(
        VersionInfo.rc.in
        ${VERSIONINFO_RC}
        @ONLY # 只处理@var@格式的变量
)

include_directories(header)

find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        Xml
        Network
        REQUIRED)

add_executable(Win_Switcher src/main.cpp
        ${VERSIONINFO_RC}
        res.qrc
        src/widget.cpp
        header/widget.h
        ui/widget.ui
        src/utils/winEventHook.cpp
        header/utils/winEventHook.h
        src/utils/Util.cpp
        header/utils/Util.h
        header/utils/setWindowBlur.h
        src/utils/setWindowBlur.cpp
        src/utils/IconOnlyDelegate.cpp
        header/utils/IconOnlyDelegate.h
        header/utils/uiautomation.h
        src/utils/uiautomation.cpp
        src/utils/AppUtil.cpp
        header/utils/AppUtil.h
        src/utils/KeyboardHooker.cpp
        header/utils/KeyboardHooker.h
        src/utils/TaskbarWheelHooker.cpp
        header/utils/TaskbarWheelHooker.h
        src/utils/QtWin.cpp
        header/utils/QtWin.h
        header/utils/ComInitializer.h
        header/utils/SingleApp.h
        header/utils/Startup.h
        header/utils/SystemTray.h
        header/utils/ConfigManager.h
        header/utils/ConfigManagerBase.h
        src/UpdateDialog.cpp
        header/UpdateDialog.h
        ui/UpdateDialog.ui
        src/utils/ScheduledTask.cpp
        header/utils/ScheduledTask.h)
target_link_libraries(Win_Switcher
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Xml
        Qt::Network
        Qt::CorePrivate   # UpdateDialog.cpp 使用了 QtCore/private/qzipreader_p.h
        Qt::GuiPrivate    # 引用 Qt6Gui_PRIVATE_INCLUDE_DIRS
        Dwmapi
        Psapi            # Util.cpp 使用 GetModuleFileNameEx
        Propsys          # AppUtil.cpp 使用 propsys (SHGetPropertyStoreFromParsingName)
        Comctl32         # commoncontrols.h
        windowsapp       # winrt/Windows.Management.Deployment.h
)

# 启用`gui_private`模块
# 必须放在find_package之后，否则`Qt6Gui_PRIVATE_INCLUDE_DIRS`变量未定义
target_include_directories(${PROJECT_NAME} PRIVATE ${Qt6Gui_PRIVATE_INCLUDE_DIRS})

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "AltTaber") # 设置输出文件名
# 用生成器表达式兼容多配置生成器（Visual Studio 下 CMAKE_BUILD_TYPE 为空）
set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)

target_compile_definitions(${PROJECT_NAME} PRIVATE
        APP_VERSION="${PROJECT_VERSION}" # 注入 QApplication::applicationVersion()（UpdateDialog 更新检查依赖）
        QT_DEPRECATED_WARNINGS
        UNICODE # for Windows API，使用W版本
        _UNICODE # for C/C++ 标准库
)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
            /utf-8
            /W4            # 开启高等级警告
            /permissive-   # 标准一致性
    )
endif ()