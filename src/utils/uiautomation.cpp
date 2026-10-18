#include "utils/uiautomation.h"
#include <windows.h>
#include <QDebug>

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

void UIAutomation::cleanup() {
    if (pAutomation) {
        pAutomation->Release();
        pAutomation = nullptr;
    }
}

QString UIElement::getName() const {
    if (!pElement) return {};

    QString res;
    BSTR name;
    if (auto hr = pElement->get_CurrentName(&name); SUCCEEDED(hr)) {
        res = QString::fromWCharArray(name);
        SysFreeString(name);
    } else {
        qWarning() << "Failed to get name." << hr;
    }
    return res;
}

QString UIElement::getClassName() const {
    if (!pElement) return {};

    QString res;
    BSTR className;
    if (auto hr = pElement->get_CurrentClassName(&className); SUCCEEDED(hr)) {
        res = QString::fromWCharArray(className);
        SysFreeString(className);
    } else {
        qWarning() << "Failed to get class name." << hr;
    }
    return res;
}

QString UIElement::getAutomationId() const {
    if (!pElement) return {};

    QString res;
    BSTR id;
    if (auto hr = pElement->get_CurrentAutomationId(&id); SUCCEEDED(hr)) {
        res = QString::fromWCharArray(id);
        SysFreeString(id);
    } else {
        qWarning() << "Failed to get automation id." << hr;
    }
    return res;
}
