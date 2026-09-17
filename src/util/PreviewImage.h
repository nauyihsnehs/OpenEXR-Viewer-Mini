#pragma once

#include <QImage>
#include <QColor>
#include <QRectF>
#include <QTransform>
#include <QRegion>
#include <model/framebuffer/FramebufferData.h>

class FramebufferModel;

namespace PreviewImage
{
    // Pixel rectangles are relative to the data-window origin, with exclusive ends.
    struct Geometry {
        Geometry(const QRect& data, const QRect& display, double aspect);
        explicit Geometry(const FramebufferModel& model);
        QRectF displayPixels;
        QRectF visiblePixels;
        QTransform imageToScene;

        QRectF sceneWindow() const;
        QSize outputSize(int maxWidth = 0) const;
        QTransform imageToOutput(const QSize& size) const;
    };

    struct Snapshot {
        QImage image;
        Geometry geometry {QRect(), QRect(), 1.};
        std::vector<FramebufferData::AnomalyRegion> anomalies;
        QRegion coverage;
    };
    Snapshot capture(const FramebufferModel& model);
    QImage render(const Snapshot& snapshot, int maxWidth = 0,
                  const QColor& background = Qt::transparent);

    // Allocate only the final output size. Null means unavailable or too large.
    QImage render(const FramebufferModel& model, int maxWidth = 0,
                  const QColor& background = Qt::transparent);
}
