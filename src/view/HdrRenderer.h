#pragma once

#include <QImage>
#include <QPixmap>
#include <QRectF>
#include <QTransform>
#include <QWidget>
#include <memory>
#include <model/framebuffer/HdrPreviewFrame.h>

// Keep the viewport object (and its overlays) alive when changing display modes.
class HdrViewport : public QWidget
{
  public:
    explicit HdrViewport(QWidget* parent) : QWidget(parent) {}
    void setHdrPainting(bool enabled);
    QPaintEngine* paintEngine() const override;
  private:
    bool m_hdrPainting = false;
};

// Windows-only implementation; no DirectX declarations leak into Qt/model headers.
class HdrRenderer
{
  public:
    HdrRenderer();
    ~HdrRenderer();
    bool refresh(WId window, bool retry = false);
    void release();
    bool available() const;
    QString statusText() const;
    QString statusDetail() const;
    bool render(WId window, QSize size, qreal devicePixelRatio,
                const HdrPreviewFrame& frame, const QTransform& imageToViewport,
                const QRectF& visiblePixels, bool smooth, const QPixmap& checkerboard,
                const QImage& markers, bool markersEnabled);
  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
