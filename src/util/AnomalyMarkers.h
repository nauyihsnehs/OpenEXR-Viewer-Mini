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
    void composite(QImage& image, const FramebufferModel& model);
}
