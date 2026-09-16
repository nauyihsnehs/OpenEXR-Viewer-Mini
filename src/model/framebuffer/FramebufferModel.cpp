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

#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <exception>
#include <algorithm>
#include <sstream>

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
    connect(
      &m_loadWatcher,
      &QFutureWatcher<DecodeResult>::finished,
      this,
      [this] {
          const DecodeResult result = m_loadWatcher.result();
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
          m_renderActive            = false;
          if (m_activeGeneration == m_generation) {
              m_error = result.error;
              if (!result.image.isNull()) {
                  m_image = result.image;
                  setReady(true);
                  emit imageChanged();
              } else if (!m_error.isEmpty()) {
                  emit readinessChanged();
                  emit loadFailed(m_error);
              }
          }
          startRender();
      });
}

FramebufferModel::~FramebufferModel()
{
    // Jobs own all their inputs; destroying a watcher does not wait for them.
    if (m_loadCancel) m_loadCancel->store(true);
    if (m_renderCancel) m_renderCancel->store(true);
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
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_loading) return;
    ++m_generation;
    if (m_renderCancel) m_renderCancel->store(true);
    m_pendingRender = Renderer();
    m_loaded        = false;
    m_loading       = true;
    m_error.clear();
    setReady(false);
    emit readinessChanged();
    m_loadCancel              = std::make_shared<std::atomic_bool>(false);
    const Cancellation cancel = m_loadCancel;
    m_loadWatcher.setFuture(QtConcurrent::run([decoder, cancel] {
        DecodeResult result;
        try {
            if (!cancel->load()) result = decoder(cancel);
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
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_loaded) return;
    ++m_generation;
    m_error.clear();
    setReady(false);
    if (m_renderCancel) m_renderCancel->store(true);
    m_pendingRender = std::move(renderer);
    startRender();
}

void FramebufferModel::startRender()
{
    if (m_renderActive || !m_pendingRender) return;
    m_renderActive            = true;
    m_activeGeneration        = m_generation;
    const Renderer render     = std::move(m_pendingRender);
    m_pendingRender           = Renderer();
    m_renderCancel            = std::make_shared<std::atomic_bool>(false);
    const Cancellation cancel = m_renderCancel;
    m_renderWatcher.setFuture(QtConcurrent::run(renderPool(), [render, cancel] {
        RenderResult result;
        try {
            if (!cancel->load()) result.image = render(cancel);
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
