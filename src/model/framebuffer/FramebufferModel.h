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

#pragma once

#include "FramebufferData.h"
#include <model/LoadProgress.h>
#include <util/EnvironmentProjection.h>
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QRegion>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
#include <utility>
#include <string>

class FramebufferModel: public QObject
{
    Q_OBJECT
  public:
    explicit FramebufferModel(QObject* parent = nullptr);
    ~FramebufferModel() override;

    const QImage&             getLoadedImage() const { return m_image; }
    EnvironmentProjection::SourceInfo environmentSource() const { return EnvironmentProjection::describe(*m_data); }
    int rawEnvmap() const { return m_data->envmap; }
    EnvironmentProjection::State projectionState() const;
    EnvironmentProjection::State requestedProjectionState() const;
    void setProjectionState(EnvironmentProjection::State state);
    void beginProjectionInteraction();
    void updateProjectionInteraction(EnvironmentProjection::State state);
    void endProjectionInteraction();
    void resetProjectionView();
    bool projectionInteracting() const { return m_projectionInteracting; }
    bool isFullPreviewReady() const { return m_ready && !m_projectionInteracting && !m_interactiveFrame; }
    QSize projectionCanvasSize() const { return m_projectionCanvas; }
    EnvironmentProjection::Snapshot projectionSnapshot() const;
    QRect previewDataWindow() const { return m_projected ? m_projected->dataWindow : getDataWindow(); }
    QRect previewDisplayWindow() const { return m_projected ? m_projected->displayWindow : getDisplayWindow(); }
    float previewPixelAspect() const { return m_projected ? 1.f : pixelAspectRatio(); }
    bool isProjected() const { return bool(m_projected); }
    std::string projectedColorInfo(int x, int y, bool compact = false) const;
    // Deep has no single raw value per pixel: its 2D buffer is the current composite.
    // Lossless Deep export must read deepSamples(), including native channel types.
    const std::vector<float>& getRawPixels() const
    {
        return m_data->sourcePixels.empty() ? m_data->pixels : m_data->sourcePixels;
    }
    const std::vector<float>& getDisplayPixels() const { return m_data->pixels; }
    const ViewMetadata& rawViews() const { return m_data->rawViews; }
    bool isDerivedPreview() const { return bool(m_data->stereo[0]); }
    bool hasDeepSamples() const { return m_data->hasDeep(); }
    const std::shared_ptr<const DeepSamples>& deepSamples() const { return m_data->deep; }
    DepthBounds depthBounds() const;
    DepthRange depthRange() const;
    void setDepthRange(DepthRange range);
    QRegion pixelCoverage() const;
    const Imf::Chromaticities* rawChromaticities() const
    {
        return m_data->hasRawChromaticities ? &m_data->rawChromaticities : nullptr;
    }
    bool                      isImageLoaded() const { return m_loaded; }
    bool                      isPreviewReady() const { return m_ready; }
    bool                      isLoading() const { return m_loading; }
    Progress loadProgress() const { return m_loading ? m_loadProgress : m_renderProgress; }
    QString                   errorString() const { return m_error; }
    int                       width() const { return m_data->width; }
    int                       height() const { return m_data->height; }
    float    pixelAspectRatio() const { return m_data->pixelAspect; }
    QRect    getDisplayWindow() const { return m_data->displayWindow; }
    QRect    getDataWindow() const { return m_data->dataWindow; }
    double   getDatasetMin() const { return m_data->minimum; }
    double   getDatasetMax() const { return m_data->maximum; }
    uint64_t getDatasetNaNCount() const { return m_data->nanCount; }
    uint64_t getDatasetInfCount() const { return m_data->infCount; }
    uint64_t getDatasetPositiveInfCount() const { return m_data->positiveInfCount; }
    uint64_t getDatasetNegativeInfCount() const { return m_data->negativeInfCount; }
    bool     hasFiniteSamples() const { return m_data->hasFiniteSamples; }
    bool highlightNonFinite() const { return m_highlightNonFinite; }
    const std::vector<FramebufferData::AnomalyRegion>& anomalyRegions() const
    {
        return m_projected ? m_projected->anomalyRegions : m_data->anomalyRegions;
    }
    void setHighlightNonFinite(bool enabled)
    {
        if (m_highlightNonFinite == enabled) return;
        m_highlightNonFinite = enabled;
        emit anomalyMarkersChanged();
    }
    virtual int rawPixelStride() const
    {
        return int(rawChannelNames().size());
    }

    ResolutionLevel resolutionLevel() const { return m_data->resolutionLevel; }
    size_t resolutionLevelCount() const { return m_data->resolutionLevels.size(); }
    virtual std::string getColorInfo(int x, int y, bool compact = false) const = 0;
    virtual std::vector<std::string> rawChannelNames() const          = 0;
    virtual std::vector<int> rawChannelComponents() const
    {
        std::vector<int> components(rawChannelNames().size());
        for (size_t i = 0; i < components.size(); ++i) components[i] = int(i);
        return components;
    }
    std::vector<QPoint> rawChannelSampling() const;
    virtual std::array<int, 3> displayRgbComponents() const;

  signals:
    void anomalyMarkersChanged();
    void imageChanged();
    void imageLoaded();
    void loadFailed(QString message);
    void readinessChanged();
    void depthRangeChanged();

  protected:
    std::string sampleLocationInfo(size_t channel, int x, int y) const;
    static std::string sampleLocationInfo(const FramebufferData& data, int component, int x, int y);
    using Decoder  = std::function<DecodeResult(const Cancellation&)>;
    using ProgressDecoder = std::function<DecodeResult(const Cancellation&, const Progress&)>;
    struct RenderResult {
        RenderResult(QImage rendered = QImage(), std::shared_ptr<const FramebufferData> snapshot = {})
          : image(std::move(rendered)), data(std::move(snapshot)) {}
        QImage image;
        std::shared_ptr<const FramebufferData> data;
        std::shared_ptr<const FramebufferData> projected;
        QRegion projectedCoverage;
        QSize projectionCanvas;
        bool interactive = false;
        EnvironmentProjection::State projectionState;
        EnvironmentProjection::ColorMapper mapColors;
        QString error;
    };
    using Renderer = std::function<RenderResult(const Cancellation&)>;
    using ProgressRenderer = std::function<RenderResult(const Cancellation&, const Progress&)>;
    void                                   startLoading(Decoder decoder);
    void                                   startLoading(ProgressDecoder decoder);
    void                                   requestRender(Renderer renderer);
    void                                   requestRender(ProgressRenderer renderer);
    static int                             renderThreadCount();
    virtual void                           updateImage() = 0;
    std::shared_ptr<const FramebufferData> m_data;
    EnvironmentProjection::Snapshot projectionInput() const;
    static RenderResult renderProjection(const std::shared_ptr<const FramebufferData>& data,
      EnvironmentProjection::Snapshot snapshot, EnvironmentProjection::ColorMapper mapper,
      const Cancellation& cancel, const Progress& progress = {});

  private:
    DepthRange m_depthRange;
    EnvironmentProjection::State m_requestedProjection, m_committedProjection;
    EnvironmentProjection::ColorMapper m_colorMapper;
    std::shared_ptr<const FramebufferData> m_projected;
    QRegion m_projectedCoverage;
    QSize m_projectionCanvas;
    std::shared_ptr<const FramebufferData> m_projectedSource;
    bool m_projectionInteracting = false, m_projectionDirty = false, m_interactiveFrame = false;
    QTimer m_projectionTimer;
    QElapsedTimer m_projectionSubmission;
    bool assignProjectionState(EnvironmentProjection::State state);
    void scheduleProjectionInteraction();
    void                         startRender();
    void                         setReady(bool ready);
    QImage                       m_image;
    QFutureWatcher<DecodeResult> m_loadWatcher;
    QFutureWatcher<RenderResult> m_renderWatcher;
    Cancellation                 m_loadCancel;
    Cancellation                 m_renderCancel;
    ProgressRenderer             m_pendingRender;
    Progress m_loadProgress, m_renderProgress;
    quint64                      m_generation       = 0;
    quint64                      m_activeGeneration = 0;
    bool                         m_renderActive     = false;
    bool                         m_loaded           = false;
    bool                         m_loading          = false;
    bool                         m_ready            = false;
    QString                      m_error;
    bool                         m_highlightNonFinite = false;
};
