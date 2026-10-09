#include "utils/uiautomation.h"
#include <windows.h>
#include <QDebug>
#include "utils/Util.h"

IUIAutomation* UIAutomation::pAutomation = nullptr;

bool UIAutomation::init() {
    qDebug() << "UIAutomation initializing";
    pAutomation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, (void**) &pAutomation);
    if (FAILED(hr) || !pAutomation) {
        qCritical() << "Failed to create UIAutomation instance." << qt_error_string(hr);
        return false;
    }

    return true;
}

/// based on physical cursor position
UIElement UIAutomation::getElementUnderMouse() {
    if (!pAutomation) {
        if (!init()) {
            qWarning() << "Failed to initialize UIAutomation.";
            return {};
        }
    }

    POINT pt;
    GetCursorPos(&pt);
    IUIAutomationElement* pElement = nullptr;
    auto hr = pAutomation->ElementFromPoint(pt, &pElement);

    if (FAILED(hr) || !pElement) {
        qWarning() << "Failed to get element under mouse." << hr << pElement << qt_error_string(hr);
        return {};
    }

    return UIElement{pElement};
}

UIElement UIAutomation::getParentWithHWND(const UIElement& element) {
    if (!pAutomation) {
        qWarning() << "UIAutomation not initialized in getParentWithHWND";
        return {};
    }
    IUIAutomationElement* pParent = nullptr;
    IUIAutomationElement* pElement = element.inner();
    IUIAutomationTreeWalker* pTreeWalker = nullptr;
    if (SUCCEEDED(pAutomation->get_ControlViewWalker(&pTreeWalker))) {
        UIA_HWND hwnd = nullptr;
        do {
            if (FAILED(pTreeWalker->GetParentElement(pElement, &pParent)) || !pParent)
                break; // 已到桌面根或失败，pParent 为 nullptr
            if (FAILED(pParent->get_CurrentNativeWindowHandle(&hwnd)))
                hwnd = nullptr; // 失败时重置，避免读未初始化值
            pElement = pParent; // 注意：pElement 由调用方持有，不 Release；pParent 所有权在下轮循环转移
        } while (pElement && !hwnd);
        pTreeWalker->Release();
    }
    return UIElement{pParent}; // 若循环未执行或失败，pParent 为 nullptr，UIElement 安全处理
}

void UIAutomation::cleanup() {
    if (pAutomation) {
        pAutomation->Release();
        pAutomation = nullptr;
    }
}

QString UIElement::getName() const {
    if (!pElement) return {};

    QString res;
    BSTR name = nullptr; // 初始化，避免失败分支读取野值
    if (auto hr = pElement->get_CurrentName(&name); SUCCEEDED(hr) && name) {
        res = QString::fromWCharArray(name, SysStringLen(name)); // BSTR 可能内嵌 NUL，按长度转换
        SysFreeString(name);
    } else {
        qWarning() << "Failed to get name." << hr;
    }
    return res;
}

QString UIElement::getClassName() const {
    if (!pElement) return {};

    QString res;
    BSTR className = nullptr;
    if (auto hr = pElement->get_CurrentClassName(&className); SUCCEEDED(hr) && className) {
        res = QString::fromWCharArray(className, SysStringLen(className));
        SysFreeString(className);
    } else {
        qWarning() << "Failed to get class name." << hr;
    }
    return res;
}

QRect UIElement::getBoundingRect() const {
    if (!pElement) return {};

    RECT rect{}; // 初始化，失败时避免读未初始化栈值
    if (FAILED(pElement->get_CurrentBoundingRectangle(&rect)))
        return {};
    return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
}

CONTROLTYPEID UIElement::getControlType() const {
    if (!pElement) return 0;

    CONTROLTYPEID type = 0;
    if (FAILED(pElement->get_CurrentControlType(&type)))
        return 0;
    return type;
}

HWND UIElement::getNativeWindowHandle() const {
    if (!pElement) return nullptr;

    UIA_HWND hwnd = nullptr;
    if (FAILED(pElement->get_CurrentNativeWindowHandle(&hwnd)))
        return nullptr;
    return (HWND) hwnd;
}

QString UIElement::getNativeWindowClass() const {
    if (!pElement) return {};

    HWND hwnd = getNativeWindowHandle();
    if (!hwnd) return {}; // 防御：避免对 nullptr 调用 GetClassName 导致未定义行为
    return Util::getClassName(hwnd);
}

QString UIElement::getSelfOrParentNativeWindowClass() const {
    if (!pElement) return {};

    HWND hwnd = getNativeWindowHandle();
    if (!hwnd)
        hwnd = UIAutomation::getParentWithHWND(*this).getNativeWindowHandle();
    if (!hwnd) return {}; // 防御：避免对 nullptr 调用 GetClassName
    return Util::getClassName(hwnd);
}

QString UIElement::getAutomationId() const {
    if (!pElement) return {};

    QString res;
    BSTR id = nullptr;
    if (auto hr = pElement->get_CurrentAutomationId(&id); SUCCEEDED(hr) && id) {
        res = QString::fromWCharArray(id, SysStringLen(id));
        SysFreeString(id);
    } else {
        qWarning() << "Failed to get automation id." << hr;
    }
    return res;
}
