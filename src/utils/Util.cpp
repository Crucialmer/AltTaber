#include <QList>
#include "utils/Util.h"
#include <appmodel.h>
#include "utils/AppUtil.h"
#include <QDebug>
#include <psapi.h>
#include <QFileInfo>
#include <QDir>
#include "utils/QtWin.h"
#include <commoncontrols.h>
#include <ShObjIdl_core.h>
#include <QDomDocument>
#include <QPainter>
#include <propkey.h>
#include <atlbase.h>
#include <minappmodel.h>
#include <tlhelp32.h>
#include <shlobj_core.h>
#include <QFileIconProvider>
#include <qoperatingsystemversion.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <QThread>
#include <QMutex>
#include <QSet>
#include <QTimer>
#include <QImage>
#include <atomic>

namespace Util {
    QString getWindowTitle(HWND hwnd) {
        const int len = GetWindowTextLength(hwnd) + 1; // include '\0'
        if (len <= 1) return {};
        auto* title = new wchar_t[len];
        GetWindowText(hwnd, title, len);
        QString result = QString::fromWCharArray(title);
        delete[] title;
        return result;
    }

    QString getClassName(HWND hwnd) { // 性能还可以，0.02ms
        const int MAX = 256;
        wchar_t className[MAX];
        GetClassName(hwnd, className, MAX);
        return QString::fromWCharArray(className);
    }

    /// 判断窗口是否被隐藏，与IsWindowVisible不同，两者需要同时判断（疑惑）
    bool isWindowCloaked(HWND hwnd) {
        BOOL isCloaked = false; // ! bool 会造成参数错误，导致本函数恒定false; bool & BOOL字节数不同，调试模式下 LLDB一栏会报错！ 非调试看不出来
        auto rt = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &isCloaked, sizeof(isCloaked));
        return rt == S_OK && isCloaked;
    }

    bool isProcessElevated(HANDLE hProcess) {
        HANDLE hToken = nullptr;
        if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
            qDebug() << "OpenProcessToken failed" << GetLastError();
            return false;
        }

        TOKEN_ELEVATION elevation;
        DWORD size;
        bool isElevated = false;

        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &size)) {
            isElevated = (elevation.TokenIsElevated != 0);
        } else {
            qDebug() << "GetTokenInformation failed" << GetLastError();
        }

        CloseHandle(hToken);
        return isElevated;
    }

    /// 判断窗口是否处于提升状态（管理员权限），有趣的是"任务管理器"是`Elevated`的，但是无需经过UAC确认，可能系统应用自动提升
    bool isWindowElevated(HWND hwnd) {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess == nullptr) {
            qDebug() << "OpenProcess failed" << GetLastError();
            return false;
        }

        bool isAdmin = isProcessElevated(hProcess);

        CloseHandle(hProcess);
        return isAdmin;
    }

    // slow
    QString getProcessPath(DWORD pid) {
        QString path;
        static const bool isWin10orHigher = (QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows10);
        HANDLE hProcess = nullptr;
        if (isWin10orHigher) // LIMIT权限基本可以open所有类型窗口
            hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        else // ↓该权限下，管理员权限窗口（如任务管理器）open失败; 1Password 虽然是非管理员窗口，但是安全系数较高，也open失败
            hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);

        // for `GetModuleFileNameEx`, 句柄必须具有 PROCESS_QUERY_INFORMATION 和 PROCESS_VM_READ 访问权限
        // but, Windows 10 及更高版本、Windows Server 2016 及更高版本：如果 hModule 参数为 NULL，则句柄只需要 PROCESS_QUERY_LIMITED_INFORMATION 访问权限
        if (hProcess) {
            TCHAR processName[MAX_PATH];
            // https://www.cnblogs.com/mooooonlight/p/14491399.html
            if (GetModuleFileNameEx(hProcess, nullptr, processName, MAX_PATH))
                path = QString::fromWCharArray(processName);
            CloseHandle(hProcess);
        }
        return path;
    }

    // slow
    QString getWindowProcessPath(HWND hwnd) {
        if (AppUtil::isAppFrameWindow(hwnd)) // AppFrame 用于焦点和窗口操作
            hwnd = AppUtil::getAppCoreWindow(hwnd); // AppCore 用于获取exe路径

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid)
            return getProcessPath(pid);
        return {};
    }

    /// 根据可执行文件路径查找进程 PID
    DWORD findProcessByPath(const QString& exePath) {
        DWORD targetPID = 0;
        // Warning: snapshot是消耗品，不能重复使用
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return 0;
        }

        PROCESSENTRY32 entry = {sizeof(PROCESSENTRY32)};
        if (Process32First(snapshot, &entry)) {
            do {
                QString processPath = getProcessPath(entry.th32ProcessID);
                if (QString::compare(processPath, exePath, Qt::CaseInsensitive) == 0) {
                    targetPID = entry.th32ProcessID;
                    break;
                }
            } while (Process32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return targetPID;
    }

    /// 获取所有子进程的路径
    QList<QString> getChildProcessPaths(DWORD parentPID) {
        QList<QString> childPaths;

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return childPaths;
        }

        PROCESSENTRY32 entry = {sizeof(PROCESSENTRY32)};
        if (Process32First(snapshot, &entry)) {
            do {
                if (entry.th32ParentProcessID == parentPID) {
                    QString processPath = getProcessPath(entry.th32ProcessID);
                    if (!processPath.isEmpty()) {
                        childPaths.append(processPath);
                    }
                }
            } while (Process32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return childPaths;
    }

    /// 根据exe路径查找所有子进程的路径
    QList<QString> getChildProcessPaths(const QString& exePath) {
        if (auto pid = findProcessByPath(exePath)) {
            return getChildProcessPaths(pid);
        }
        return {};
    }

    QString getFileDescription(const QString& path) {
        QString desc = QFileInfo(path).completeBaseName(); // fallback to base name

        // 使用 CComPtr 自动释放 IShellItem2 接口
        CComPtr<IShellItem2> pItem;
        HRESULT hr = SHCreateItemFromParsingName(path.toStdWString().c_str(), nullptr, IID_PPV_ARGS(&pItem));
        if (SUCCEEDED(hr)) {
            // 使用 CComHeapPtr 自动释放字符串（调用 CoTaskMemFree）
            CComHeapPtr<WCHAR> pValue;
            hr = pItem->GetString(PKEY_FileDescription, &pValue);
            if (SUCCEEDED(hr))
                desc = QString::fromWCharArray(pValue);
            else
                qWarning() << "No FileDescription, fallback to file name:" << desc;
        } else {
            qWarning() << "SHCreateItemFromParsingName() failed";
        }

        return desc;
    }

    bool isTopMost(HWND hwnd) {
        return GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST;
    }

    /// 将焦点从[自身]切换至[指定窗口]
    /// <br> 注意：如果自身没有焦点，可能失败 if without `force` arg
    void switchToWindow(HWND hwnd, bool force) {
        if (IsIconic(hwnd))
            ShowWindow(hwnd, SW_RESTORE);
        const HWND prevForeground = GetForegroundWindow();
        if (force) { // 强制措施(hack)
            // 如果本进程没有前台窗口，则Windows不允许抢占焦点，此时需要hack技巧
            // 此处模拟任意按键均可（除了LAlt，因为我们正按着）; 推测是满足了：调用进程收到了最后一个输入事件
            // !!但是如果模拟RAlt的话，会导致切换后目标窗口的Menu处于焦点状态，导致滚轮和键盘操作不符预期，还会导致于Alt相关快捷键意外触发(Alt+Space)
            // doc: https://learn.microsoft.com/zh-cn/windows/win32/api/winuser/nf-winuser-setforegroundwindow?redirectedfrom=MSDN
            // ref: https://github.com/stianhoiland/cmdtab/blob/746c41226cdd820c26eadf00eb86b45896dc1dcd/src/cmdtab.c#L144
            // ref: https://stackoverflow.com/questions/10740346/setforegroundwindow-only-working-while-visual-studio-is-open
            // ref: https://stackoverflow.com/questions/53706056/how-to-activate-window-started-by-another-process
            // ref: [Foreground activation permission is like love: You can’t steal it, it has to be given to you]
            //      https://devblogs.microsoft.com/oldnewthing/20090220-00/?p=19083
            // 还有一种比较常见的方法是：AttachThreadInput
            // 另一个技巧是： AllocConsole ?
            // ref: https://github.com/sigoden/window-switcher/blob/0c99b27823b4e6abd21e73f505a7c2cd8c21f59b/src/utils/window.rs#L122

            // Hack: 这里我们模拟一个空的键盘输入即可，目的是让本进程成为“最近产生输入的进程”，以获取设置前台窗口的权限
            // ref: https://github.com/stianhoiland/cmdtab/blob/d33730f52af40f46545b086953158a5382f2a05b/src/cmdtab.c#L488
            // ref: https://github.com/microsoft/PowerToys/blob/7d0304fd06939d9f552e75be9c830db22f8ff9e2/src/modules/fancyzones/FancyZonesLib/util.cpp#L376
            INPUT input = {.type = INPUT_KEYBOARD};
            SendInput(1, &input, sizeof(INPUT));
            SetForegroundWindow(hwnd);
        } else {
            // 如果本进程has前台窗口，则可以随意调用该函数转移焦点
            SetForegroundWindow(hwnd);
        }
        // 回退链：非管理员进程对高权限窗口 SetForegroundWindow 会静默失败（UIPI 拦截），
        // 此时降级为"最小破坏"方案：前置 Z 序（不激活）+ 恢复最小化 + 系统闪烁提示，
        // 保证窗口至少可见并处于任务栏最前，用户一次点击即可获得焦点。
        if (GetForegroundWindow() != hwnd && prevForeground != hwnd) {
            qWarning() << "SetForegroundWindow failed (elevated target?), fallback to topmost-without-activate:" << hwnd;
            if (IsIconic(hwnd))
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (!IsIconic(hwnd))
                FlashWindow(hwnd, TRUE); // 任务栏闪烁提示（非激活）
        }
    }

    /// bring to Top(Z-Order), but not activate it<br>
    /// note: ignore topmost window
    void bringWindowToTop(HWND hwnd, HWND hWndInsertAfter) {
        if (isTopMost(hwnd)) return;
        LockSetForegroundWindow(LSFW_LOCK); // avoid focus stolen
        if (IsIconic(hwnd))
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        // HWND_TOP not works well
        // ref: https://stackoverflow.com/questions/5257977/how-to-bring-other-app-window-to-front-without-activating-it
        // 给一个基准HWND相较于HWND_TOPMOST更稳定，防止hwnd抢占焦点或倒反天罡遮挡，同时增加Z序调整成功率
        // such as: 特别是Windows Terminal，不仅抢焦点、遮挡，有时候还toTop失败; 不过 hWndInsertAfter == this->hWnd()就好多了
        SetWindowPos(hwnd, hWndInsertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // 如果 `hWndInsertAfter` 是TOPMOST，那么hwnd也会变成TOPMOST
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        LockSetForegroundWindow(LSFW_UNLOCK);
    }

    /// filter HWND by some rules
    /// @param skipVisibleCheck skip IsWindowVisible check<br>
    /// 在窗口创建过程中，会触发 EVENT_SYSTEM_FOREGROUND，但是这瞬间 IsWindowVisible 为false
    bool isWindowAcceptable(HWND hwnd, bool skipVisibleCheck) {
        static const QStringList BlackList_ClassName = {
            "Progman",
            "Windows.UI.Core.CoreWindow", // 过滤UWP Core，从Frame入手
            "CEF-OSC-WIDGET",
            "WorkerW", // explorer.exe
            "Shell_TrayWnd" // explorer.exe
        };
        static const QStringList BlackList_ExePath = {
            R"(C:\Windows\System32\wscript.exe)"
        };
        static const QStringList BlackList_FileName = { // TODO by user from config
            "Nahimic3.exe",
            "Follower.exe",
            "QQ Follower.exe"
        };
        LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        QString className;

        if ((skipVisibleCheck || IsWindowVisible(hwnd))
            && !isWindowCloaked(hwnd)
            // 窗口显示在任务栏的基本规则：https://devblogs.microsoft.com/oldnewthing/20031229-00/?p=41283
            && (!GetWindow(hwnd, GW_OWNER) || exStyle & WS_EX_APPWINDOW) // OmApSvcBroker, QQ主面板（意料之外）; 保留：系统属性（Path）
            && (exStyle & WS_EX_TOOLWINDOW) == 0 // 非工具窗口，但其实有些工具窗口没有这个这个属性
            //            && (exStyle & WS_EX_TOPMOST) == 0 // 非置顶窗口
            && GetWindowTextLength(hwnd) > 0
            && (className = getClassName(hwnd)).size() > 0 // cache
            && !BlackList_ClassName.contains(className)
            && !className.startsWith("imestatuspop_classname{") // 输入法（的推销弹窗）https://s3.bmp.ovh/imgs/2024/12/23/bb136fde101a41ce.png
        ) {
            auto path = getWindowProcessPath(hwnd); // 耗时操作，减少次数
            if (!BlackList_ExePath.contains(path) && !BlackList_FileName.contains(QFileInfo(path).fileName()))
                return true;
        }
        return false;
    }

    BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
        if (isWindowAcceptable(hwnd)) {
            auto* windowList = reinterpret_cast<QList<HWND>*>(lParam);
            windowList->append(hwnd);
        }
        return TRUE;
    }

    QList<HWND> enumWindows() {
        QList<HWND> list;
        // only enum top-level windows
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&list));
        return list;
    }

    BOOL CALLBACK EnumChildWindowsProc(HWND hwnd, LPARAM lParam) {
        auto* windowList = reinterpret_cast<QList<HWND>*>(lParam);
        windowList->append(hwnd);
        return TRUE;
    }

    QList<HWND> enumChildWindows(HWND hwnd) {
        QList<HWND> list;
        EnumChildWindows(hwnd, EnumChildWindowsProc, reinterpret_cast<LPARAM>(&list));
        return list;
    }

    // about 2ms
    QList<HWND> listValidWindows() {
        qDebug() << "#List Valid Windows";
        static const bool isUserAdmin = IsUserAnAdmin(); // 和 isProcessElevated(GetCurrentProcess()) 好像没区别？
        using namespace AppUtil;
        QList<HWND> list;
        const auto winList = Util::enumWindows();
        for (auto hwnd: winList) {
            if (!hwnd) continue;
            // 高权限窗口：非管理员运行时也纳入列表（switchToWindow 内置 SetForegroundWindow
            // 失败时的回退链，见其注释），最大化非管理员模式下的可用功能。
            // 管理员窗口的 exe 路径经 PROCESS_QUERY_LIMITED_INFORMATION 仍可读取（图标/标题正常）。
            if (!isUserAdmin && isWindowElevated(hwnd)) {
                qDebug() << "#elevated window included (non-admin mode):" << hwnd << getWindowTitle(hwnd);
            }

            /* fix `isWindowCloaked()`之后，以下代码无用
            auto className = Util::getClassName(hwnd);
            // ref: https://blog.csdn.net/qq_59075481/article/details/139574981
            if (className == AppFrameWindowClass) { // UWP的父窗口
                const auto childList = Util::enumChildWindows(hwnd);
                auto title = getWindowTitle(hwnd);
                HWND coreChild = nullptr;
                for (HWND child: childList) {
                    // UWP的本体应该是`Windows.UI.Core.CoreWindow`，但是enumWindows有时候枚举不到 [因为只能枚举顶层窗口]
                    // 只能通过`ApplicationFrameWindow`曲线救国 [正道]
                    // 但是有些情况下（最小化），UWP的本体又会从`ApplicationFrameWindow`中分离出来，不属于子窗口，两种情况都要处理
                    if (getWindowTitle(child) == title && getClassName(child) == AppCoreWindowClass) {
                        coreChild = child;
                        break;
                    }
                }
                if (!coreChild) {
                    // 对于正常UWP窗口，Core只在Frame最小化时脱离AppFrame，变成top-level
                    // 如果Core是顶层，且AppFrame不是最小化，那么就是非正常窗口，舍弃
                    // ！！有个例外，对于"便笺"不适用，会误判：便笺的Core的标题和Frame不同，不是"便笺"是"Title"，同时存在另一个"便签"Core (顶层)
                    if (!IsIconic(hwnd) || !FindWindowW(LPCWSTR(AppCoreWindowClass.utf16()), LPCWSTR(title.utf16()))) {
                        // 本来可以用于排除不可见的"设置" & "电影和电视" & "Realtek Audio Console"
                        // ！！但是fix `isWindowCloaked()`之后，已经可以被其正确过滤了...
                        qDebug() << "#ignore UWP:" << title << hwnd;
                        continue;
                    }
                }
            }*/

            list << hwnd;
        }
        return list;
    }

    /// list windows filtered by exePath
    QList<HWND> listValidWindows(const QString& exePath) {
        QList<HWND> windows;
        const auto winList = listValidWindows();
        for (auto hwnd: winList) {
            if (getWindowProcessPath(hwnd).compare(exePath, Qt::CaseInsensitive) == 0) // 用 == 就寄了，特别是taskbar
                windows << hwnd;
        }
        return windows;
    }

    /// Multi-windows version of `FindWindow`
    /// <br> Only for top-level windows
    QList<HWND> findTopWindows(const QString& className, const QString& title) {
        // EnumWindows 枚举不到 TaskListThumbnailWnd 很奇怪，明明是顶层窗口
        QList<HWND> windows;
        auto pClassName = className.isNull() ? nullptr : LPCWSTR(className.utf16());
        auto pTitle = title.isNull() ? nullptr : LPCWSTR(title.utf16());

        if (HWND hwnd = FindWindow(pClassName, pTitle)) {
            windows << hwnd;
            // 按照Z序往下
            // NULL 代表父窗口为桌面窗口
            while ((hwnd = FindWindowEx(nullptr, hwnd, pClassName, pTitle)))
                windows << hwnd;
        }

        return windows;
    }

    /// Get 256x256 icon
    // 不用Index，直接用SHGetFileInfo获取图标的话，最大只能32x32
    // 对于QFileIconProvider的优势是可以多线程
    // ref: https://github.com/stianhoiland/cmdtab/blob/746c41226cdd820c26eadf00eb86b45896dc1dcd/src/cmdtab.c#L333
    // ref: https://blog.csdn.net/ssss_sj/article/details/9786403
    // ExtractIconEx 最大返回32x32
    // IShellItemImageFactory::GetImage 获取的图像有锯齿（64x64），而256x256倒是好一点，但是若exe没有这么大的图标，缩放后还是会很小（中心）
    // SHGetFileInfo 获取的图标最大只能32x32, 但是可以通过Index + SHGetImageList获取更大的图标(Jumbo)，这就是QFileIconProvider的实现
    // 没办法，对于不包含大图标的exe，周围会被填充透明，导致真实图标很小（例如，[Follower]获取64x64的图标，但只有左上角有8x8图标，其余透明）
    // 更诡异的是，48x48的Icon，Follower是可以正常获取的，比64x64的实际Icon尺寸还要大，倒行逆施
    // 但是我无法得知真实图标大小，无法进行缩放，只能作罢
    QIcon getJumboIcon(const QString& filePath) {
        SHFILEINFOW sfi = {nullptr};
        // Get the icon index using SHGetFileInfo
        SHGetFileInfo(filePath.toStdWString().c_str(), 0, &sfi, sizeof(SHFILEINFOW), SHGFI_SYSICONINDEX);

        // 48x48 icons, use SHIL_EXTRALARGE
        // 256x256 icons (after Vista), use SHIL_JUMBO
        IImageList* imageList = nullptr; // 初始化：失败时避免 Release 野指针
        HRESULT hResult = SHGetImageList(SHIL_JUMBO, IID_IImageList, (void**) &imageList);

        QIcon icon;
        if (hResult == S_OK) {
            HICON hIcon;
            hResult = imageList->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hIcon);

            if (hResult == S_OK) {
                icon = QtWin::fromHICON(hIcon);
                DestroyIcon(hIcon); // GetIcon 返回的句柄需显式销毁；原代码误销毁了 sfi.hIcon（SHGFI_SYSICONINDEX 模式下该字段无效）
            }
        }
        if (imageList)
            imageList->Release(); // 失败时 imageList 为 nullptr，跳过
        return icon;
    }

    bool isBottomRightTransparent(const QIcon& icon, int extent = 32) {
        // 8 16 32 64貌似时间都差不多 怪
        QImage image = icon.pixmap(extent).toImage().convertToFormat(QImage::Format_ARGB32); // 强制转换为支持透明的格式

        int width = image.width();
        int height = image.height();

        // 遍历右下角 1/4 区域
        for (int y = height / 2; y < height; ++y) {
            for (int x = width / 2; x < width; ++x) {
                if (qAlpha(image.pixel(x, y)) != 0) // 检查 Alpha 通道是否为 0
                    return false;
            }
        }
        return true;
    }

    /// 通过窗口句柄获取UWP安装目录，如果该窗口不是UWP应用，则返回""
    QString getUwpInstallDirFromHwnd(HWND hwnd) {
        if (AppUtil::isAppFrameWindow(hwnd))
            hwnd = AppUtil::getAppCoreWindow(hwnd); // AppCore 用于获取exe路径
        // 仅通过`isAppFrameWindow(hwnd)`来判断UWP可能不够准确

        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProcess) {
            qWarning() << "OpenProcess failed" << GetLastError();
            return {};
        }

        WCHAR packageFullName[PACKAGE_FULL_NAME_MAX_LENGTH + 1] = {0};
        UINT32 length = _countof(packageFullName);
        if (auto result = GetPackageFullName(hProcess, &length, packageFullName); result != ERROR_SUCCESS) {
            if (result == ERROR_INSUFFICIENT_BUFFER)
                qWarning() << "Buffer too small for packageFullName";
            CloseHandle(hProcess);
            return {}; // not UWP, no packageFullName
        }
        CloseHandle(hProcess);

        // 其实`GetPackagePathByFullName`也可以，就是想随便用一下`WinRT` Just
        using namespace winrt;
        using namespace Windows::Management::Deployment;
        // init_apartment(apartment_type::single_threaded); // Qt 内部已经初始化了

        try {
            PackageManager packageManager;
            // `FindPackage`需要管理员权限，而`FindPackageForUser(L"", ...)` (当前用户)不需要
            auto package = packageManager.FindPackageForUser(L"", hstring(packageFullName));
            if (!package) {
                qDebug() << "Package not found?";
                return {};
            }
            return QString::fromStdWString(package.InstalledPath().c_str());
        } catch (const hresult_error& ex) {
            qWarning() << "PackageManager Error:" << QString::fromStdWString(ex.message().c_str());
        }

        return {};
    }

    // ----------------- 后台图标预取（工作线程提取 QImage；GUI 线程转换为 QIcon） -----------------
    namespace {
        QMutex g_iconPrefetchMutex;              // 保护以下两个容器
        QHash<QString, QImage> g_iconImageCache; // 后台提取好的图标图像（GUI 转换后即移除，控制内存）
        QSet<QString> g_iconResolvedPaths;       // GUI 线程已得到最终图标的 path，后台不再提取

        /// 供后台线程使用：提取 Jumbo 图标为 QImage（全程不创建 QPixmap/QIcon，线程安全）
        QImage extractJumboIconImage(const QString& filePath) {
            SHFILEINFOW sfi = {nullptr};
            SHGetFileInfo(filePath.toStdWString().c_str(), 0, &sfi, sizeof(SHFILEINFOW), SHGFI_SYSICONINDEX);

            IImageList* imageList = nullptr;
            HRESULT hResult = SHGetImageList(SHIL_JUMBO, IID_IImageList, (void**) &imageList);

            QImage image;
            if (hResult == S_OK) {
                HICON hIcon;
                hResult = imageList->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hIcon);
                if (hResult == S_OK) {
                    image = QImage::fromHICON(hIcon);
                    DestroyIcon(hIcon);
                }
            }
            if (imageList)
                imageList->Release();
            return image;
        }

        /// 后台任务体：枚举当前所有窗口进程，提前提取尚未缓存的图标
        void prefetchIconsWorker() {
            // 工作线程单独初始化 COM（SHGetImageList 依赖；主线程已初始化 STA，此处安全再入）
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            QElapsedTimer timer;
            timer.start();
            int count = 0;
            QSet<QString> seen;
            for (auto hwnd: listValidWindows()) {
                auto path = getWindowProcessPath(hwnd);
                if (path.isEmpty() || seen.contains(path)) continue;
                seen.insert(path);
                {
                    QMutexLocker locker(&g_iconPrefetchMutex);
                    if (g_iconResolvedPaths.contains(path) || g_iconImageCache.contains(path))
                        continue; // 已有最终结果或已被预取
                }
                auto image = extractJumboIconImage(path);
                if (image.isNull()) continue;
                {
                    QMutexLocker locker(&g_iconPrefetchMutex);
                    if (!g_iconResolvedPaths.contains(path)) // 期间 GUI 可能已抢先给出最终结果
                        g_iconImageCache.insert(path, image);
                }
                count++;
            }
            if (count > 0)
                qDebug() << "Icon prefetch:" << count << "icons in" << timer.elapsed() << "ms";
            if (SUCCEEDED(hr))
                CoUninitialize();
        }
    } // anonymous namespace

    /// 从预取缓存取出图像（取出即删除；GUI 线程调用）
    QImage takePrefetchedIconImage(const QString& path) {
        QMutexLocker locker(&g_iconPrefetchMutex);
        return g_iconImageCache.take(path);
    }

    /// 标记 path 已由 GUI 线程得到最终图标
    void markIconResolved(const QString& path) {
        QMutexLocker locker(&g_iconPrefetchMutex);
        g_iconResolvedPaths.insert(path);
    }

    /// 启动后台图标预取（GUI 线程调用一次）：启动后 10s 首跑，此后每 60s 一次
    void startIconPrefetch() {
        static bool started = false;
        if (started)
            return;
        started = true;
        static std::atomic_bool busy{false};
        auto runOnce = [] {
            bool expected = false;
            if (!busy.compare_exchange_strong(expected, true))
                return; // 上一轮仍在运行
            auto* thread = QThread::create([] {
                prefetchIconsWorker();
                busy.store(false);
            });
            QObject::connect(thread, &QThread::finished, thread, &QThread::deleteLater);
            thread->start();
        };
        static QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, runOnce);
        timer.start(60 * 1000);
        QTimer::singleShot(10 * 1000, runOnce);
        qDebug() << "Icon prefetch scheduled (first in 10s, then every 60s)";
    }

    /// Cached Icon, including UWP<br>
    /// `hwnd` is for getting UWP package full name<br>
    /// 如果想要直接通过exe path获取icon，就只能通过`FindPackagesForUser`枚举不太优雅<br>
    /// 让`hwnd`成为唯一参数也可以，就是要重新获取一次 path from hwnd，效率较低
    QIcon getCachedIcon(const QString& path, HWND hwnd) {
        static QHash<QString, QIcon> IconCache;
        if (auto icon = IconCache.value(path); !icon.isNull())
            return icon;

        QElapsedTimer t;
        t.start();
        QIcon icon;

        // 1.不太好通过exe是否包含图标判断UWP，因为SystemSettings.exe居然包含图标！
        // 2.AppxManifest.xml 和 exe不一定在同目录，如："MSI Center" "Notepad"，通过`FindPackagesForUser`枚举不太优雅
        // 3.后来发现可以通过pid获取packageFullName，进而获取Package对象，得到安装目录
        if (auto uwpDir = getUwpInstallDirFromHwnd(hwnd); !uwpDir.isEmpty()) { // UWP (MS Store App)
            qDebug() << "Detect UWP" << path;
            icon = AppUtil::getAppIcon(uwpDir + "\\fake.exe");
        } else { // win32 desktop app
            if (auto img = takePrefetchedIconImage(path); !img.isNull()) { // 后台预取命中：直接转换，零等待
                icon = QIcon(QPixmap::fromImage(img));
            } else {
                icon = getJumboIcon(path);
            }
            if (isBottomRightTransparent(icon)) {
                // 对于不包含大图标的exe，例如[Follower]获取64x64的图标，但只有左上角有8x8图标，其余透明）
                // 通过检测右下角1/4区域来判定这种小图标情况，改为获取小号size图标，则会正常（如48x48）（也许需要向下遍历，但是一般情况下够用了）
                qDebug() << "-- BottomRight is transparent, fallback to 48x48" << path;
                icon = QFileIconProvider().icon(QFileInfo(path)).pixmap(48);
            }
        }
        IconCache.insert(path, icon);
        markIconResolved(path); // 已得到最终图标，通知后台预取跳过该 path
        qDebug() << "Icon not found in cache, loaded in" << t.elapsed() << "ms" << path;
        return icon;
    }

    /// 获取窗口的关联图标，例如任务栏图标（旧版QQ会设置当前好友头像），或资源管理器当前文件夹图标（但是任务栏不会变化）
    /// <br> Normally 32x32, & not support UWP
    QPixmap getWindowIcon(HWND hwnd) {
        auto hIcon = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0));
        if (!hIcon) // 这种方式能获取更多图标，例如当窗口没有使用SETICON时
            hIcon = reinterpret_cast<HICON>(GetClassLongPtr(hwnd, GCLP_HICON));
        return QtWin::fromHICON(hIcon);
    }

    /// 设置窗口圆角 原来这么方便嘛！ 为什么Qt搜不到！
    // https://github.com/stianhoiland/cmdtab/blob/746c41226cdd820c26eadf00eb86b45896dc1dcd/src/cmdtab.c#L1275
    // https://github.com/DinoChan/WindowChromeApplyRoundedCorners
    bool setWindowRoundCorner(HWND hwnd, DWM_WINDOW_CORNER_PREFERENCE pvAttribute) {
        HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pvAttribute, sizeof(pvAttribute));
        if (FAILED(hr)) {
            qWarning() << "Failed to set rounded corners for window:" << hr;
            return false;
        }
        return true;
    }

    /// Convenience function of `GetAsyncKeyState`
    bool isKeyPressed(int vkey) {
        return GetAsyncKeyState(vkey) & 0x8000;
    }

    /// Overlay icon
    QIcon overlayIcon(const QPixmap& icon, const QPixmap& overlay, const QRect& overlayRect) {
        QPixmap bg = icon;
        QPainter painter(&bg);
        painter.drawPixmap(overlayRect, overlay);
        painter.end();
        return bg;
    }

    /// Input: 需要物理坐标，不要用`QCursor::pos`（逻辑坐标），特别是开启了DPI缩放的情况下
    HWND topWindowFromPoint(const POINT& pos) {
        HWND hwnd = WindowFromPoint(pos);
        return GetAncestor(hwnd, GA_ROOT);
    }

    /// Get physical cursor position
    POINT getCursorPos() {
        POINT pos;
        GetCursorPos(&pos);
        return pos;
    }

    /// Get "TaskListThumbnailWnd" of current(cursor) monitor(screen)
    HWND getCurrentTaskListThumbnailWnd() {
        const QList<HWND> thumbs = findTopWindows("TaskListThumbnailWnd");
        POINT cursorPos = getCursorPos();
        const auto cursorMonitor = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTONEAREST);
        for (HWND hwnd: thumbs) {
            if (auto monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL); monitor == cursorMonitor)
                return hwnd;
        }
        return nullptr;
    }

    bool isTaskbarWindow(HWND hwnd) {
        auto className = Util::getClassName(hwnd);
        return className == QStringLiteral("Shell_TrayWnd") || className == QStringLiteral("Shell_SecondaryTrayWnd"); // 副屏
    }
} // Util
