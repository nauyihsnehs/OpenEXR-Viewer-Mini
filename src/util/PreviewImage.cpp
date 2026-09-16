#include "PreviewImage.h"
#include "AnomalyMarkers.h"
#include <model/framebuffer/FramebufferModel.h>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <limits>

PreviewImage::Geometry::Geometry(const QRect& data, const QRect& display, double aspect)
    : displayPixels(double(display.x()) - data.x(), double(display.y()) - data.y(),
                    display.width(), display.height()),
      visiblePixels(displayPixels.intersected(QRectF(0., 0., data.width(), data.height()))),
      imageToScene(QTransform::fromScale(aspect, 1.))
{}

PreviewImage::Geometry::Geometry(const FramebufferModel& model)
    : Geometry(model.getDataWindow(), model.getDisplayWindow(), model.pixelAspectRatio())
{}

QRectF PreviewImage::Geometry::sceneWindow() const
{
    return imageToScene.mapRect(displayPixels);
}

QSize PreviewImage::Geometry::outputSize(int maxWidth) const
{
    const double aspect = imageToScene.m11();
    if (displayPixels.isEmpty() || !std::isfinite(aspect) || aspect <= 0.) return {};
    double width = std::max(1., std::round(displayPixels.width() * aspect));
    double height = displayPixels.height();
    if (!std::isfinite(width)) return {};
    if (maxWidth > 0 && width > maxWidth) {
        height = std::max(1., std::round(height * (maxWidth / width)));
        width = maxWidth;
    }
    // Match the loader's Qt image/stride limit, checking before integer conversion.
    if (width * height > double(std::numeric_limits<int>::max()) / 4.) return {};
    return QSize(int(width), int(height));
}

QTransform PreviewImage::Geometry::imageToOutput(const QSize& size) const
{
    const double sx = size.width() / displayPixels.width();
    const double sy = size.height() / displayPixels.height();
    return QTransform(sx, 0., 0., sy, -displayPixels.x() * sx, -displayPixels.y() * sy);
}

QImage PreviewImage::render(const FramebufferModel& model, int maxWidth, const QColor& background)
{
    if (!model.isPreviewReady()) return {};
    const QImage source = model.getLoadedImage();
    if (source.isNull()) return {};
    const Geometry geometry(model);
    const QSize size = geometry.outputSize(maxWidth);
    if (size.isEmpty()) return {};
    QImage output(size, QImage::Format_ARGB32_Premultiplied);
    if (output.isNull()) return {};
    output.fill(background);
    if (!geometry.visiblePixels.isEmpty()) {
        QPainter painter(&output);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setTransform(geometry.imageToOutput(size));
        painter.setClipRect(geometry.visiblePixels);
        painter.drawImage(geometry.visiblePixels, source, geometry.visiblePixels);
    }
    AnomalyMarkers::composite(output, model);
    return output;
}
