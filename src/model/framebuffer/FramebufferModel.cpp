/**
 * Copyright (c) 2021 Alban Fichet <alban dot fichet at gmx dot fr>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *  * Neither the name of the organization(s) nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "FramebufferModel.h"
#include <cmath>

#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <exception>
#include <algorithm>
#include <sstream>
#include "DeepPreview.h"
#include "PixelDiagnostics.h"

EnvironmentProjection::State FramebufferModel::projectionState() const
{ return EnvironmentProjection::resolve(m_committedProjection, *m_data); }
EnvironmentProjection::State FramebufferModel::requestedProjectionState() const
{ return EnvironmentProjection::resolve(m_requestedProjection, *m_data); }

bool FramebufferModel::assignProjectionState(EnvironmentProjection::State state)
{
    if (!isImageLoaded() || !std::isfinite(state.yaw) || !std::isfinite(state.pitch)
        || !std::isfinite(state.fieldOfView)) return false;
    state = EnvironmentProjection::resolve(state, *m_data);
    if (state.type < EnvironmentProjection::LatLong || state.type > EnvironmentProjection::Sphere) return false;
    if (!environmentSource().available() && state.type != m_data->envmap) return false;
    state.yaw = std::remainder(state.yaw, 360.);
    state.pitch = std::max(-90., std::min(90., state.pitch));
    state.fieldOfView = std::max(10., std::min(150., state.fieldOfView));
    if (state == requestedProjectionState()) return false;
    m_requestedProjection = state;
    return true;
}

void FramebufferModel::setProjectionState(EnvironmentProjection::State state)
{
    const bool finish = m_projectionInteracting || m_interactiveFrame;
    m_projectionTimer.stop();
    m_projectionInteracting = m_projectionDirty = false;
    if (assignProjectionState(state) || finish) updateImage();
    emit readinessChanged();
}

void FramebufferModel::resetProjectionView()
{
    auto state = requestedProjectionState();
    state.yaw = state.pitch = 0.; state.fieldOfView = 90.;
    setProjectionState(state);
}

void FramebufferModel::beginProjectionInteraction()
{
    const auto type = requestedProjectionState().type;
    if (m_projectionInteracting || !environmentSource().available()
        || (type != EnvironmentProjection::Perspective && type != EnvironmentProjection::Sphere)) return;
    m_projectionInteracting = true;
    emit readinessChanged();
}

void FramebufferModel::updateProjectionInteraction(EnvironmentProjection::State state)
{
    if (state.type != requestedProjectionState().type) return;
    beginProjectionInteraction();
    if (!m_projectionInteracting || !assignProjectionState(state)) return;
    m_projectionDirty = true;
    scheduleProjectionInteraction();
}

void FramebufferModel::scheduleProjectionInteraction()
{
    if (!m_projectionInteracting || !m_projectionDirty || m_renderActive || m_pendingRender) return;
    const qint64 elapsed = m_projectionSubmission.isValid() ? m_projectionSubmission.elapsed() : 34;
    if (elapsed < 34) { m_projectionTimer.start(int(34 - elapsed)); return; }
    updateImage(); // Capture only the newest input; do not invalidate a running interaction frame.
}

void FramebufferModel::endProjectionInteraction()
{
    if (!m_projectionInteracting) return;
    m_projectionTimer.stop();
    m_projectionInteracting = m_projectionDirty = false;
    // A click without movement does not need a render (including double-click to minimal view).
    if (m_interactiveFrame || m_renderActive || !(requestedProjectionState() == projectionState()))
        updateImage(); // Supersede every unfinished interaction generation with a full frame.
    emit readinessChanged();
}

EnvironmentProjection::Snapshot FramebufferModel::projectionInput() const
{
    EnvironmentProjection::Snapshot snapshot;
    snapshot.source = m_data;
    snapshot.state = requestedProjectionState();
    snapshot.stride = rawPixelStride();
    snapshot.names = rawChannelNames();
    snapshot.components = rawChannelComponents();
    snapshot.interactive = m_projectionInteracting;
    if (m_projectedSource == m_data) {
        snapshot.cachedProjection = m_projected;
        snapshot.cachedState = m_committedProjection;
    }
    return snapshot;
}

EnvironmentProjection::Snapshot FramebufferModel::projectionSnapshot() const
{
    auto snapshot = projectionInput();
    snapshot.state = projectionState();
    snapshot.mapColors = m_colorMapper;
    snapshot.markers = highlightNonFinite();
    snapshot.complete = isFullPreviewReady();
    snapshot.interactive = false;
    snapshot.cachedProjection.reset();
    return snapshot;
}

FramebufferModel::RenderResult FramebufferModel::renderProjection(
  const std::shared_ptr<const FramebufferData>& data, EnvironmentProjection::Snapshot snapshot,
  EnvironmentProjection::ColorMapper mapper, const Cancellation& cancel, const Progress& progress)
{
    RenderResult result;
    result.data = data;
    result.projectionState = snapshot.state;
    result.mapColors = mapper;
    result.interactive = snapshot.interactive;
    if (data->envmap >= 0 && snapshot.state.type != data->envmap) {
        snapshot.source = data;
        result.projectionCanvas = EnvironmentProjection::defaultSize(*data, snapshot.state.type);
        QSize raster = result.projectionCanvas;
        if (snapshot.interactive && std::max(raster.width(), raster.height()) > 512)
            raster.scale(512, 512, Qt::KeepAspectRatio);
        const auto cached = snapshot.cachedProjection;
        if (cached && snapshot.cachedState == snapshot.state
            && QSize(cached->width, cached->height) == raster)
            result.projected = cached;
        else
            result.projected = EnvironmentProjection::project(snapshot, snapshot.state, raster, cancel, renderThreadCount(), progress);
        if (!result.projected) return {};
        result.projectedCoverage = EnvironmentProjection::coverage(*result.projected);
    }
    result.image = mapper(result.projected ? *result.projected : *data, cancel);
    return cancel->load() ? RenderResult() : result;
}

std::string FramebufferModel::projectedColorInfo(int x, int y, bool compact) const
{
    if (!m_projected || x < 0 || y < 0 || x >= m_projected->width || y >= m_projected->height) return "";
    const size_t p = size_t(y) * m_projected->width + x;
    if (!m_projected->covers(p)) return "";
    QPointF position;
    if (!EnvironmentProjection::sourcePosition(*m_data, projectionState(),
        QSize(m_projected->width, m_projected->height), x, y, position)) return "";
    std::ostringstream text;
    const auto names = rawPixelStride() == 1 ? rawChannelNames() : std::vector<std::string>{"R", "G", "B", "A"};
    if (compact) {
        text << "≈(" << std::setprecision(5) << position.x() << ", " << position.y() << ")  ";
        const std::vector<int> components = rawPixelStride() == 1
          ? std::vector<int>{0} : std::vector<int>{0, 1, 2, 3};
        text << PixelDiagnostics::compactChannels(names, components,
          &m_projected->pixels[p * rawPixelStride()]);
        return text.str();
    }
    text << "Level " << resolutionLevel().toString() << " | Source sample ("
         << std::setprecision(9) << position.x() << ", " << position.y() << ") | Interpolated linear";
    for (size_t c = 0; c < names.size(); ++c)
        text << " " << names[c] << ": " << PixelDiagnostics::sampleText(m_projected->pixels[p * rawPixelStride() + c]);
    return text.str();
}

DepthBounds FramebufferModel::depthBounds() const { return DeepPreview::bounds(*m_data); }
DepthRange FramebufferModel::depthRange() const { return depthBounds().clamp(m_depthRange); }
void FramebufferModel::setDepthRange(DepthRange range)
{
    if (!hasDeepSamples() || !std::isfinite(range.minimum) || !std::isfinite(range.maximum)
        || range.minimum > range.maximum) return;
    range = depthBounds().clamp(range);
    if (range == depthRange()) return;
    m_depthRange = range;
    emit depthRangeChanged();
    updateImage();
}

std::vector<QPoint> FramebufferModel::rawChannelSampling() const
{
    std::vector<QPoint> sampling;
    for (int component : rawChannelComponents())
        sampling.push_back(m_data->sourceSampling[component]);
    return sampling;
}

std::string FramebufferModel::sampleLocationInfo(size_t channel, int x, int y) const
{
    return sampleLocationInfo(*m_data, rawChannelComponents()[channel], x, y);
}

QRegion FramebufferModel::pixelCoverage() const
{
    if (m_projected) return m_projectedCoverage;
    if (!isDerivedPreview()) return QRect(0, 0, width(), height());
    QRegion coverage;
    for (const auto& eye : m_data->stereo)
        coverage += QRect(eye->dataWindow.topLeft() - m_data->dataWindow.topLeft(), eye->dataWindow.size());
    return coverage;
}

std::string FramebufferModel::sampleLocationInfo(const FramebufferData& data, int component, int x, int y)
{
    const QPoint sampling = data.sourceSampling[component];
    if (sampling == QPoint(1, 1)) return "";
    const auto align = [](int64_t value, int minimum, int maximum, int step) {
        const auto floorDivide = [](int64_t v, int s) {
            return v / s - (v % s < 0 ? 1 : 0);
        };
        const int64_t first = -floorDivide(-int64_t(minimum), step);
        const int64_t last = floorDivide(maximum, step);
        return std::max(first, std::min(last, floorDivide(value, step))) * step;
    };
    const QRect window = data.dataWindow;
    std::stringstream text;
    text << " @ (" << align(int64_t(window.x()) + x, window.left(), window.right(), sampling.x())
         << ", " << align(int64_t(window.y()) + y, window.top(), window.bottom(), sampling.y()) << ")";
    return text.str();
}

std::array<int, 3> FramebufferModel::displayRgbComponents() const
{
    const auto names = rawChannelNames();
    if (names.size() == 1) {
        const auto dot = names[0].find_last_of('.');
        const auto leaf = names[0].substr(dot == std::string::npos ? 0 : dot + 1);
        if (leaf == "R") return {{0, -1, -1}};
        if (leaf == "G") return {{-1, 0, -1}};
        if (leaf == "B") return {{-1, -1, 0}};
    }
    return {{0, 0, 0}};
}

namespace
{
    QThreadPool* renderPool()
    {
        static QThreadPool pool;
        static const bool  configured = [] {
            pool.setMaxThreadCount(2);
            return true;
        }();
        Q_UNUSED(configured);
        return &pool;
    }
}   // namespace

FramebufferModel::FramebufferModel(QObject* parent)
  : QObject(parent)
  , m_data(std::make_shared<FramebufferData>())
{
    m_projectionTimer.setSingleShot(true);
    m_projectionTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_projectionTimer, &QTimer::timeout, this, &FramebufferModel::scheduleProjectionInteraction);
    connect(
      &m_loadWatcher,
      &QFutureWatcher<DecodeResult>::finished,
      this,
      [this] {
          const DecodeResult result = m_loadWatcher.result();
          if (m_loadProgress) m_loadProgress->stop();
          m_loading                 = false;
          m_error                   = result.error;
          if (!result.data) {
              emit readinessChanged();
              if (!m_error.isEmpty()) emit loadFailed(m_error);
              return;
          }
          m_data   = result.data;
          m_loaded = true;
          emit imageLoaded();
          updateImage();
      });
    connect(
      &m_renderWatcher,
      &QFutureWatcher<RenderResult>::finished,
      this,
      [this] {
          const RenderResult result = m_renderWatcher.result();
          if (m_activeGeneration == m_generation && m_renderProgress) m_renderProgress->stop();
          m_renderActive            = false;
          if (m_activeGeneration == m_generation) {
              m_error = result.error;
              if (!result.image.isNull()) {
                  if (result.data) m_data = result.data;
                  m_projected = result.projected;
                  m_projectedSource = result.projected ? result.data : nullptr;
                  m_projectionCanvas = result.projectionCanvas;
                  m_interactiveFrame = result.interactive;
                  m_projectedCoverage = result.projectedCoverage;
                  m_committedProjection = result.projectionState;
                  m_colorMapper = result.mapColors;
                  m_image = result.image;
                  setReady(true);
                  emit imageChanged();
              } else if (!m_error.isEmpty()) {
                  m_projectionTimer.stop();
                  m_projectionInteracting = m_projectionDirty = false;
                  m_requestedProjection = m_committedProjection;
                  if (hasDeepSamples()) {
                      m_depthRange = m_data->depthRange;
                      emit depthRangeChanged();
                  }
                  emit readinessChanged();
                  emit loadFailed(m_error);
              }
          }
          startRender();
          scheduleProjectionInteraction();
      });
}

FramebufferModel::~FramebufferModel()
{
    // Jobs own all their inputs; destroying a watcher does not wait for them.
    if (m_loadCancel) m_loadCancel->store(true);
    if (m_renderCancel) m_renderCancel->store(true);
    if (m_loadProgress) m_loadProgress->stop();
    if (m_renderProgress) m_renderProgress->stop();
}

int FramebufferModel::renderThreadCount()
{
    // Two concurrent previews share at most eight CPU workers.
    return std::max(1, std::min(4, QThread::idealThreadCount() / 2));
}

void FramebufferModel::setReady(bool ready)
{
    if (m_ready == ready) return;
    m_ready = ready;
    emit readinessChanged();
}

void FramebufferModel::startLoading(Decoder decoder)
{
    startLoading([decoder](const Cancellation& cancel, const Progress&) { return decoder(cancel); });
}

void FramebufferModel::startLoading(ProgressDecoder decoder)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_loading) return;
    m_projectionTimer.stop();
    m_projectionInteracting = m_projectionDirty = false;
    ++m_generation;
    if (m_renderCancel) m_renderCancel->store(true);
    m_pendingRender = ProgressRenderer();
    m_loaded        = false;
    m_loading       = true;
    m_error.clear();
    setReady(false);
    emit readinessChanged();
    m_loadCancel              = std::make_shared<std::atomic_bool>(false);
    const Cancellation cancel = m_loadCancel;
    m_loadProgress = std::make_shared<LoadProgress>(m_generation);
    m_loadProgress->begin(LoadProgress::Decoding);
    const Progress progress = m_loadProgress;
    m_loadWatcher.setFuture(QtConcurrent::run(imageLoadPool(), [decoder, cancel, progress] {
        DecodeResult result;
        try {
            if (!cancel->load()) result = decoder(cancel, progress);
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
        } catch (...) {
            result.error = QObject::tr("Unable to decode image.");
        }
        return result;
    }));
}

void FramebufferModel::requestRender(Renderer renderer)
{
    requestRender([renderer](const Cancellation& cancel, const Progress&) { return renderer(cancel); });
}

void FramebufferModel::requestRender(ProgressRenderer renderer)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_loaded) return;
    ++m_generation;
    if (m_projectionInteracting) {
        m_projectionDirty = false;
        m_projectionSubmission.restart();
    }
    m_error.clear();
    setReady(false);
    if (m_renderCancel) m_renderCancel->store(true);
    if (m_renderProgress) m_renderProgress->stop();
    m_renderProgress = std::make_shared<LoadProgress>(m_generation);
    m_renderProgress->begin(LoadProgress::Rendering);
    m_pendingRender = std::move(renderer);
    startRender();
}

void FramebufferModel::startRender()
{
    if (m_renderActive || !m_pendingRender) return;
    m_renderActive            = true;
    m_activeGeneration        = m_generation;
    const ProgressRenderer render = std::move(m_pendingRender);
    m_pendingRender           = ProgressRenderer();
    m_renderCancel            = std::make_shared<std::atomic_bool>(false);
    const Cancellation cancel = m_renderCancel;
    const Progress progress = m_renderProgress;
    m_renderWatcher.setFuture(QtConcurrent::run(renderPool(), [render, cancel, progress] {
        RenderResult result;
        try {
            if (!cancel->load()) result = render(cancel, progress);
            progress->stop();
            if (result.image.isNull() && !cancel->load())
                result.error = QObject::tr("Unable to allocate preview image.");
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
        } catch (...) {
            result.error = QObject::tr("Unable to render preview.");
        }
        return result;
    }));
}
