#include "utils/DwmThumbnail.h"

#include <QDebug>

DwmThumbnail::DwmThumbnail(HWND target) : target_(target) {}

DwmThumbnail::~DwmThumbnail() {
    unregister();
}

bool DwmThumbnail::setSource(HWND source) {
    if (source && source == source_ && thumbnail_) {
        return true; // 源未变，保留现有注册，避免重复注册造成闪烁
    }

    unregister();

    if (!source || source == target_) {
        return false;
    }

    HTHUMBNAIL thumbnail = nullptr;
    const HRESULT hr = DwmRegisterThumbnail(target_, source, &thumbnail);
    if (FAILED(hr) || !thumbnail) {
        qWarning() << "DwmRegisterThumbnail failed" << static_cast<unsigned long>(hr)
                   << "source" << source << "target" << target_;
        return false;
    }

    thumbnail_ = thumbnail;
    source_ = source;
    return true;
}

void DwmThumbnail::setDestinationRect(const RECT& rect) {
    if (!thumbnail_) {
        return;
    }

    DWM_THUMBNAIL_PROPERTIES props{};
    props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    props.rcDestination = rect;
    props.fVisible = TRUE;
    props.opacity = 255;
    props.fSourceClientAreaOnly = TRUE;

    const HRESULT hr = DwmUpdateThumbnailProperties(thumbnail_, &props);
    if (FAILED(hr)) {
        qWarning() << "DwmUpdateThumbnailProperties failed" << static_cast<unsigned long>(hr)
                   << "source" << source_;
    }
}

void DwmThumbnail::hide() {
    if (!thumbnail_) {
        return;
    }

    DWM_THUMBNAIL_PROPERTIES props{};
    props.dwFlags = DWM_TNP_VISIBLE;
    props.fVisible = FALSE;

    const HRESULT hr = DwmUpdateThumbnailProperties(thumbnail_, &props);
    if (FAILED(hr)) {
        qWarning() << "DwmUpdateThumbnailProperties(hide) failed" << static_cast<unsigned long>(hr)
                   << "source" << source_;
    }
}

void DwmThumbnail::unregister() {
    if (!thumbnail_) {
        return;
    }
    DwmUnregisterThumbnail(thumbnail_);
    thumbnail_ = nullptr;
    source_ = nullptr;
}
