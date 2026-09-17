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

#include "FramebufferModel.h"
#include "ToneMapping.h"
#include <model/ExrInput.h>
#include <util/ColormapModule.h>

#include <memory>
#include <array>

class RGBFramebufferModel: public FramebufferModel
{
  public:
    enum LayerType
    {
        Layer_RGB,
        Layer_Y,
        Layer_YC,
    };

    enum PreviewMode
    {
        Preview_Exposure,
        Preview_ToneMapping,
        Preview_FalseColor,
    };

    typedef ToneMapping::Method    ToneMappingMethod;
    static const ToneMappingMethod Tone_Reinhard = ToneMapping::Reinhard;
    static const ToneMappingMethod Tone_ACES     = ToneMapping::Aces;
    static const ToneMappingMethod Tone_Filmic   = ToneMapping::Filmic;
    static const ToneMappingMethod Tone_Log      = ToneMapping::Logarithmic;
    static const ToneMappingMethod Tone_Clamp    = ToneMapping::Clamp;

    RGBFramebufferModel(
      const std::string& parentLayerName,
      LayerType          layerType = Layer_RGB,
      QObject*           parent    = nullptr);

    virtual ~RGBFramebufferModel();

    struct Input {
        Input() = default;
        Input(int partId, LayerType type, const std::array<std::string, 4>& names)
          : part(partId), layout(type), channels(names) {}
        int part = 0;
        LayerType layout = Layer_RGB;
        std::array<std::string, 4> channels;
    };
    void loadStereo(const std::shared_ptr<ExrInput>& file, const std::array<Input, 2>& eyes, ResolutionLevel level = {});

    virtual void load(
      const std::shared_ptr<ExrInput>& file,
      int                                             partId,
      const std::array<std::string, 4>&               channels, ResolutionLevel level = {});

    virtual std::string getColorInfo(int x, int y) const;

    virtual float                    getRedInfo(int x, int y) const;
    virtual float                    getGreenInfo(int x, int y) const;
    virtual float                    getBlueInfo(int x, int y) const;
    virtual float                    getAlphaInfo(int x, int y) const;
    virtual std::vector<std::string> rawChannelNames() const;
    std::vector<int> rawChannelComponents() const override;
    int rawPixelStride() const override { return 4; }
    std::array<int, 3> displayRgbComponents() const override { return {{0, 1, 2}}; }
    double getLuminanceMin() const { return m_data->luminanceMin; }
    double getLuminanceMax() const { return m_data->luminanceMax; }
    bool   hasFiniteLuminanceSamples() const
    {
        return m_data->hasFiniteLuminance;
    }

  public slots:
    void setPreviewMode(PreviewMode mode);
    void setToneMappingMethod(ToneMappingMethod method);
    void setFalseColorColormap(ColormapModule::Map map);
    void setFalseColorRange(double min, double max);
    void setExposure(double value);
    void setToneParameters(double p0, double p1, double p2, double p3);

  protected:
    void updateImage();

  private:
    float                           component(int x, int y, int channel) const;
    std::string                     m_parentLayer;
    LayerType                       m_layerType;
    std::array<std::string, 4>       m_channels;
    PreviewMode                     m_previewMode;
    ToneMappingMethod               m_toneMappingMethod;
    double                          m_exposure;
    double                          m_toneParams[4];
    double                          m_falseColorMin;
    double                          m_falseColorMax;
    std::shared_ptr<const Colormap> m_falseColorMap;
};
