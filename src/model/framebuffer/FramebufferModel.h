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
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QRegion>
#include <functional>
#include <string>

class FramebufferModel: public QObject
{
    Q_OBJECT
  public:
    explicit FramebufferModel(QObject* parent = nullptr);
    ~FramebufferModel() override;

    const QImage&             getLoadedImage() const { return m_image; }
    const std::vector<float>& getRawPixels() const
    {
        return m_data->sourcePixels.empty() ? m_data->pixels : m_data->sourcePixels;
    }
    const std::vector<float>& getDisplayPixels() const { return m_data->pixels; }
    const ViewMetadata& rawViews() const { return m_data->rawViews; }
    bool isDerivedPreview() const { return bool(m_data->stereo[0]); }
    QRegion pixelCoverage() const;
    const Imf::Chromaticities* rawChromaticities() const
    {
        return m_data->hasRawChromaticities ? &m_data->rawChromaticities : nullptr;
    }
    bool                      isImageLoaded() const { return m_loaded; }
    bool                      isPreviewReady() const { return m_ready; }
    bool                      isLoading() const { return m_loading; }
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
        return m_data->anomalyRegions;
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

    virtual std::string              getColorInfo(int x, int y) const = 0;
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

  protected:
    std::string sampleLocationInfo(size_t channel, int x, int y) const;
    static std::string sampleLocationInfo(const FramebufferData& data, int component, int x, int y);
    using Decoder  = std::function<DecodeResult(const Cancellation&)>;
    using Renderer = std::function<QImage(const Cancellation&)>;
    void                                   startLoading(Decoder decoder);
    void                                   requestRender(Renderer renderer);
    static int                             renderThreadCount();
    virtual void                           updateImage() = 0;
    std::shared_ptr<const FramebufferData> m_data;

  private:
    struct RenderResult {
        QImage  image;
        QString error;
    };
    void                         startRender();
    void                         setReady(bool ready);
    QImage                       m_image;
    QFutureWatcher<DecodeResult> m_loadWatcher;
    QFutureWatcher<RenderResult> m_renderWatcher;
    Cancellation                 m_loadCancel;
    Cancellation                 m_renderCancel;
    Renderer                     m_pendingRender;
    quint64                      m_generation       = 0;
    quint64                      m_activeGeneration = 0;
    bool                         m_renderActive     = false;
    bool                         m_loaded           = false;
    bool                         m_loading          = false;
    bool                         m_ready            = false;
    QString                      m_error;
    bool                         m_highlightNonFinite = false;
};
