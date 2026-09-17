#pragma once

#include <QRectF>
#include <QRegion>
#include <model/framebuffer/FramebufferData.h>
#include <QTransform>

class FramebufferModel;
class QImage;
class QPainter;

namespace AnomalyMarkers
{
    // Coordinates and marker sizes are in the painter's target logical pixels.
    void draw(QPainter& painter, const FramebufferModel& model,
              const QTransform& imageToTarget, const QRectF& clip);
    void draw(QPainter& painter, const std::vector<FramebufferData::AnomalyRegion>& regions,
              const QRectF& visible, const QRegion& coverage,
              const QTransform& imageToTarget, const QRectF& clip);
    // Image already contains the display-window canvas at its final output size.
    void composite(QImage& image, const FramebufferModel& model);
}
