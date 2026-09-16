#pragma once

#include <QRectF>
#include <QTransform>

class FramebufferModel;
class QImage;
class QPainter;

namespace AnomalyMarkers
{
    // Coordinates and marker sizes are in the painter's target logical pixels.
    void draw(QPainter& painter, const FramebufferModel& model,
              const QTransform& imageToTarget, const QRectF& clip);
    // Image already contains the display-window canvas at its final output size.
    void composite(QImage& image, const FramebufferModel& model);
}
