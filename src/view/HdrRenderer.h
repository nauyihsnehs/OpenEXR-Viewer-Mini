#pragma once

#include <QImage>
#include <QPixmap>
#include <QRectF>
#include <QTransform>
#include <QWidget>
#include <functional>
#include <memory>
#include <model/framebuffer/HdrPreviewFrame.h>

// This HWND belongs exclusively to DXGI; the parent viewport always uses Qt painting.
class HdrSurface : public QWidget
{
  public:
    HdrSurface(QWidget* parent, std::function<void()> render);
    void clearRenderCallback() { m_render = nullptr; }
    QPaintEngine* paintEngine() const override;
  protected:
    void paintEvent(QPaintEvent* event) override;
  private:
    std::function<void()> m_render;
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
