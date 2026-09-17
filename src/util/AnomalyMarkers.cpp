#include "AnomalyMarkers.h"
#include "PreviewImage.h"

#include <model/framebuffer/FramebufferModel.h>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr double markerSize = 12.;

    struct Marker {
        QRectF bounds;
        uint8_t flags;
        bool circle;
        bool active;
    };

    template<class Action>
    void forCells(const QRectF& bounds, const QRectF& clip, double cellSize, Action action)
    {
        const QRectF visible = bounds.intersected(clip);
        const int left = int(std::floor((visible.left() - clip.left()) / cellSize));
        const int top = int(std::floor((visible.top() - clip.top()) / cellSize));
        const int right = int(std::floor((visible.right() - clip.left()) / cellSize));
        const int bottom = int(std::floor((visible.bottom() - clip.top()) / cellSize));
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x)
                action(y * 33 + x);
    }

    QColor color(uint8_t flags)
    {
        if (flags & FramebufferData::NaN) return QColor(255, 0, 255);
        if (flags & FramebufferData::PositiveInf) return QColor(0, 255, 255);
        return QColor(255, 255, 0);
    }

    std::vector<Marker> layout(const std::vector<FramebufferData::AnomalyRegion>& regions, const QRectF& visible,
                              const QTransform& transform, const QRectF& clip)
    {
        std::vector<Marker> markers;
        std::unordered_map<int, std::unordered_set<size_t>> grid;
        // Bound the grid to at most 33 x 33 cells, including boundary cells.
        // All marker sizes use the same path, even for full-resolution exports.
        const double cellSize = std::max({32., clip.width() / 32., clip.height() / 32.});
        for (const auto& region : regions) {
            const QRectF bounds = QRectF(region.bounds).intersected(visible);
            if (bounds.isEmpty()) continue;
            const QRectF mapped = transform.mapRect(bounds);
            const bool circle = region.pixelCount == 1;
            const QSizeF size = circle ? QSizeF(markerSize, markerSize)
              : QSizeF(std::max(markerSize, mapped.width() + 4.),
                       std::max(markerSize, mapped.height() + 4.));
            Marker marker {QRectF(mapped.center() - QPointF(size.width() / 2.,
                                                          size.height() / 2.), size),
                           region.flags, circle, true};
            if (!marker.bounds.intersects(clip)) continue;

            bool merged;
            do {
                merged = false;
                std::unordered_set<size_t> candidates;
                forCells(marker.bounds, clip, cellSize, [&](int cell) {
                    const auto found = grid.find(cell);
                    if (found != grid.end())
                        candidates.insert(found->second.begin(), found->second.end());
                });
                for (size_t index : candidates) {
                    Marker& other = markers[index];
                    if (!marker.bounds.intersects(other.bounds)) continue;
                    forCells(other.bounds, clip, cellSize, [&](int cell) {
                        auto found = grid.find(cell);
                        found->second.erase(index);
                        if (found->second.empty()) grid.erase(found);
                    });
                    marker.bounds = marker.bounds.united(other.bounds);
                    marker.flags |= other.flags;
                    marker.circle = false;
                    other.active = false;
                    merged = true;
                }
                // An enlarged box can now overlap markers in additional cells.
            } while (merged);

            const size_t index = markers.size();
            markers.push_back(marker);
            forCells(marker.bounds, clip, cellSize, [&](int cell) {
                grid[cell].insert(index);
            });
        }
        return markers;
    }
}

void AnomalyMarkers::draw(QPainter& painter, const FramebufferModel& model,
                         const QTransform& imageToTarget, const QRectF& clip)
{
    if (!model.isImageLoaded() || !model.highlightNonFinite() || clip.isEmpty()) return;
    draw(painter, model.anomalyRegions(), PreviewImage::Geometry(model).visiblePixels,
         model.pixelCoverage(), imageToTarget, clip);
}

void AnomalyMarkers::draw(QPainter& painter, const std::vector<FramebufferData::AnomalyRegion>& regions,
                         const QRectF& visible, const QRegion& covered,
                         const QTransform& imageToTarget, const QRectF& clip)
{
    if (clip.isEmpty() || covered.isEmpty() || regions.empty()) return;
    const auto markers = layout(regions, visible, imageToTarget, clip);
    painter.save();
    painter.setClipRect(clip, Qt::IntersectClip);
    {
        QPainterPath coverage;
        coverage.addRegion(covered);
        painter.setClipPath(imageToTarget.map(coverage), Qt::IntersectClip);
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(1.);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setBrush(Qt::NoBrush);
    for (const Marker& marker : markers) {
        if (!marker.active) continue;
        // Keep a containing region visible even when zoomed inside its bounds.
        const QRectF bounds = marker.circle ? marker.bounds
          : marker.bounds.intersected(clip.adjusted(2., 2., -2., -2.));
        if (bounds.isEmpty()) continue;
        for (int pass = 0; pass < 2; ++pass) {
            QPen pen(pass == 0 ? QColor(Qt::black) : color(marker.flags),
                     pass == 0 ? 3.5 : 1.5);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            if (marker.circle) painter.drawEllipse(bounds);
            else painter.drawRect(bounds);
        }
    }
    painter.restore();
}

void AnomalyMarkers::composite(QImage& image, const FramebufferModel& model)
{
    if (image.isNull() || !model.highlightNonFinite() || !model.isImageLoaded()
        || model.anomalyRegions().empty()) return;
    // Output marker sizes are physical image pixels, irrespective of any DPI metadata.
    const double ratio = image.devicePixelRatio();
    image.setDevicePixelRatio(1.);
    {
        QPainter painter(&image);
        const PreviewImage::Geometry geometry(model);
        if (!geometry.displayPixels.isEmpty()) {
            const QTransform transform = geometry.imageToOutput(image.size());
            draw(painter, model, transform,
                 transform.mapRect(geometry.visiblePixels).intersected(QRectF(image.rect())));
        }
    }
    image.setDevicePixelRatio(ratio);
}
