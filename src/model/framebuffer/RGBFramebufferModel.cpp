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

#include "RGBFramebufferModel.h"
#include "FramebufferLoader.h"
#include <util/ColorTransform.h>
#include <cmath>
#include <sstream>

RGBFramebufferModel::RGBFramebufferModel(
  const std::string& parentLayerName, LayerType layerType, QObject* parent)
  : FramebufferModel(parent)
  , m_parentLayer(parentLayerName)
  , m_layerType(layerType)
  , m_previewMode(Preview_Exposure)
  , m_toneMappingMethod(Tone_Reinhard)
  , m_exposure(0.)
  , m_toneParams {0.18, 4., 0., 0.}
  , m_falseColorMin(0.)
  , m_falseColorMax(1.)
  , m_falseColorMap(ColormapModule::create(ColormapModule::TURBO))
{}

RGBFramebufferModel::~RGBFramebufferModel() = default;

void RGBFramebufferModel::load(
  const std::shared_ptr<ExrInput>& file,
  int                                             partId,
  const std::array<std::string, 4>&               channels)
{
    const auto layout = m_layerType == Layer_RGB ? FramebufferLoader::RGB
                        : m_layerType == Layer_YC
                          ? FramebufferLoader::Chroma
                          : FramebufferLoader::Luminance;
    startLoading([file, partId, channels, layout](const Cancellation& cancel) {
        return FramebufferLoader::decode(
          file,
          partId,
          layout,
          channels,
          cancel);
    });
}

std::string RGBFramebufferModel::getColorInfo(int x, int y) const
{
    if (!isImageLoaded() || x < 0 || x >= width() || y < 0 || y >= height())
        return "";
    const float*      pixel = &getRawPixels()[4 * (size_t(y) * width() + x)];
    std::stringstream text;
    text << "x: " << x << " y: " << y << " | "
         << " R: " << pixel[0] << " G: " << pixel[1] << " B: " << pixel[2]
         << " A: " << pixel[3]
         << " Y: " << ToneMapping::luminance(pixel[0], pixel[1], pixel[2]);
    return text.str();
}

float RGBFramebufferModel::component(int x, int y, int channel) const
{
    if (!isImageLoaded() || x < 0 || x >= width() || y < 0 || y >= height())
        return 0.f;
    return getRawPixels()[4 * (size_t(y) * width() + x) + channel];
}
float RGBFramebufferModel::getRedInfo(int x, int y) const
{
    return component(x, y, 0);
}
float RGBFramebufferModel::getGreenInfo(int x, int y) const
{
    return component(x, y, 1);
}
float RGBFramebufferModel::getBlueInfo(int x, int y) const
{
    return component(x, y, 2);
}
float RGBFramebufferModel::getAlphaInfo(int x, int y) const
{
    return component(x, y, 3);
}
std::vector<std::string> RGBFramebufferModel::rawChannelNames() const
{
    return {"R", "G", "B", "A"};
}

void RGBFramebufferModel::setExposure(double value)
{
    if (!std::isfinite(value) || m_exposure == value) return;
    m_exposure = value;
    updateImage();
}
void RGBFramebufferModel::setPreviewMode(PreviewMode mode)
{
    if (m_previewMode == mode) return;
    m_previewMode = mode;
    updateImage();
}
void RGBFramebufferModel::setToneMappingMethod(ToneMappingMethod method)
{
    if (m_toneMappingMethod == method) return;
    m_toneMappingMethod = method;
    updateImage();
}
void RGBFramebufferModel::setFalseColorColormap(ColormapModule::Map map)
{
    m_falseColorMap.reset(ColormapModule::create(map));
    updateImage();
}
void RGBFramebufferModel::setFalseColorRange(double min, double max)
{
    if (!std::isfinite(min) || !std::isfinite(max) || min > max) return;
    if (m_falseColorMin == min && m_falseColorMax == max) return;
    m_falseColorMin = min;
    m_falseColorMax = max;
    updateImage();
}
void RGBFramebufferModel::setToneParameters(
  double p0, double p1, double p2, double p3)
{
    const double values[] = {p0, p1, p2, p3};
    for (double value : values)
        if (!std::isfinite(value)) return;
    if (
      m_toneParams[0] == p0 && m_toneParams[1] == p1 && m_toneParams[2] == p2
      && m_toneParams[3] == p3)
        return;
    std::copy(values, values + 4, m_toneParams);
    updateImage();
}

void RGBFramebufferModel::updateImage()
{
    if (!isImageLoaded()) return;
    const auto                 data     = m_data;
    const auto                 mode     = m_previewMode;
    const auto                 method   = m_toneMappingMethod;
    const float                exposure = std::exp2(m_exposure);
    const std::array<float, 4> params   = {
      {float(m_toneParams[0]),
       float(m_toneParams[1]),
       float(m_toneParams[2]),
       float(m_toneParams[3])}};
    const double minimum  = m_falseColorMin;
    const double maximum  = m_falseColorMax;
    const auto   colormap = m_falseColorMap;
    requestRender(
      [data, mode, method, exposure, params, minimum, maximum, colormap](
        const Cancellation& cancel) {
          QImage image(data->width, data->height, QImage::Format_RGBA8888);
          if (image.isNull()) return image;
          uchar*     bits    = image.bits();
          const auto stride  = image.bytesPerLine();
          const int  threads = renderThreadCount();
          Q_UNUSED(threads);
#pragma omp parallel for num_threads(                                          \
    threads) if (data->pixels.size() >= 1048576)
          for (int y = 0; y < data->height; ++y) {
              if (cancel->load()) continue;
              uchar*       line = bits + size_t(y) * stride;
              const float* pixels
                = data->pixels.data() + size_t(y) * data->width * 4;
              for (int x = 0; x < data->width; ++x) {
                  const float* pixel = pixels + 4 * x;
                  float        rgb[3];
                  if (mode == Preview_FalseColor) {
                      colormap->getRGBValue(
                        ToneMapping::luminance(pixel[0], pixel[1], pixel[2]),
                        minimum,
                        maximum,
                        rgb);
                  } else {
                      for (int c = 0; c < 3; ++c)
                          rgb[c]
                            = mode == Preview_ToneMapping
                                ? ToneMapping::toSrgb(
                                    pixel[c],
                                    method,
                                    params[0],
                                    params[1],
                                    params[2],
                                    params[3])
                                : ColorTransform::to_sRGB(exposure * pixel[c]);
                  }
                  for (int c = 0; c < 3; ++c)
                      line[4 * x + c] = ToneMapping::toByte(rgb[c]);
                  line[4 * x + 3] = ToneMapping::toByte(pixel[3]);
              }
          }
          return cancel->load() ? QImage() : image;
      });
}
