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
#include <util/ResolutionLevels.h>
#include "PixelDiagnostics.h"
#include "FramebufferLoader.h"
#include "DeepPreview.h"
#include <util/ColorTransform.h>
#include <cmath>
#include <sstream>
#include <limits>
#include <stdexcept>

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

namespace {
    FramebufferLoader::Layout decodeLayout(RGBFramebufferModel::LayerType type)
    {
        return type == RGBFramebufferModel::Layer_RGB ? FramebufferLoader::RGB
             : type == RGBFramebufferModel::Layer_YC ? FramebufferLoader::Chroma
                                                     : FramebufferLoader::Luminance;
    }
}

void RGBFramebufferModel::loadStereo(const std::shared_ptr<ExrInput>& file,
                                     const std::array<Input, 2>& eyes, ResolutionLevel level)
{
    m_channels = {};
    startLoading([file, eyes, level](const Cancellation& cancel) -> DecodeResult {
        if (!file || !file->file) throw std::runtime_error("No source image.");
        if (!ViewMetadata::stereoGeometryMatches(file->file->header(eyes[0].part), file->file->header(eyes[1].part), level))
            throw std::runtime_error("Stereo views require identical display windows and pixel aspect ratios.");
        auto data = std::make_shared<FramebufferData>();
        for (size_t i = 0; i < eyes.size(); ++i) {
            if (cancel->load()) return DecodeResult();
            auto decoded = FramebufferLoader::decode(file, eyes[i].part,
              decodeLayout(eyes[i].layout), eyes[i].channels, cancel, level);
            if (!decoded.data) return decoded;
            data->stereo[i] = decoded.data;
            data->stereoChannels[i] = eyes[i].channels;
        }
        const auto& left = *data->stereo[0];
        const auto& right = *data->stereo[1];
        if (left.displayWindow != right.displayWindow || left.pixelAspect != right.pixelAspect)
            throw std::runtime_error("Stereo resolution levels require identical display windows and pixel aspect ratios.");
        data->resolutionLevel = level;
        data->resolutionLevels = ResolutionLevels::intersection(left.resolutionLevels, right.resolutionLevels);
        const int x = std::min(left.dataWindow.left(), right.dataWindow.left());
        const int y = std::min(left.dataWindow.top(), right.dataWindow.top());
        const int64_t width = int64_t(std::max(left.dataWindow.right(), right.dataWindow.right())) - x + 1;
        const int64_t height = int64_t(std::max(left.dataWindow.bottom(), right.dataWindow.bottom())) - y + 1;
        const int64_t limit = std::numeric_limits<int>::max() / 4;
        if (width <= 0 || height <= 0 || width > limit || height > limit || width * height > limit)
            throw std::runtime_error("The stereo image is too large to display safely.");
        data->width = int(width);
        data->height = int(height);
        data->dataWindow = QRect(x, y, data->width, data->height);
        data->displayWindow = left.displayWindow;
        data->pixelAspect = left.pixelAspect;
        for (const auto& eye : data->stereo) {
            data->nanCount += eye->nanCount;
            data->infCount += eye->infCount;
            data->positiveInfCount += eye->positiveInfCount;
            data->negativeInfCount += eye->negativeInfCount;
            if (eye->hasFiniteSamples) {
                data->minimum = data->hasFiniteSamples ? std::min(data->minimum, eye->minimum) : eye->minimum;
                data->maximum = data->hasFiniteSamples ? std::max(data->maximum, eye->maximum) : eye->maximum;
                data->hasFiniteSamples = true;
            }
            if (eye->hasFiniteLuminance) {
                data->luminanceMin = data->hasFiniteLuminance ? std::min(data->luminanceMin, eye->luminanceMin) : eye->luminanceMin;
                data->luminanceMax = data->hasFiniteLuminance ? std::max(data->luminanceMax, eye->luminanceMax) : eye->luminanceMax;
                data->hasFiniteLuminance = true;
            }
            const QPoint offset = eye->dataWindow.topLeft() - data->dataWindow.topLeft();
            for (auto region : eye->anomalyRegions) {
                if (cancel->load()) return DecodeResult();
                region.bounds.translate(offset);
                data->anomalyRegions.push_back(region);
            }
        }
        if (cancel->load()) return DecodeResult();
        DecodeResult result;
        result.data = data;
        return result;
    });
}

void RGBFramebufferModel::load(
  const std::shared_ptr<ExrInput>& file,
  int                                             partId,
  const std::array<std::string, 4>&               channels, ResolutionLevel level)
{
    m_channels = channels;
    const auto layout = decodeLayout(m_layerType);
    startLoading([file, partId, channels, layout, level](const Cancellation& cancel) {
        return FramebufferLoader::decode(
          file,
          partId,
          layout,
          channels,
          cancel, level);
    });
}

std::string RGBFramebufferModel::getColorInfo(int x, int y) const
{
    if (isProjected()) return projectedColorInfo(x, y);
    if (!isImageLoaded() || x < 0 || x >= width() || y < 0 || y >= height())
        return "";
    if (isDerivedPreview()) {
        const QPoint position = getDataWindow().topLeft() + QPoint(x, y);
        if (!pixelCoverage().contains(QPoint(x, y))) return "";
        std::stringstream text;
        if (resolutionLevelCount() > 1) text << "Level " << resolutionLevel().toString() << " | ";
        text << "x: " << position.x() << " y: " << position.y();
        for (size_t i = 0; i < 2; ++i) {
            const auto& eye = *m_data->stereo[i];
            text << (i == 0 ? " | Left:" : " | Right:");
            if (!eye.dataWindow.contains(position)) {
                text << " no data";
                continue;
            }
            const QPoint local = position - eye.dataWindow.topLeft();
            const size_t index = size_t(local.y()) * eye.width + local.x();
            if (!eye.covers(index)) { text << " No samples"; continue; }
            if (eye.deep) text << " Composite";
            const auto& raw = eye.deep || eye.sourcePixels.empty() ? eye.pixels : eye.sourcePixels;
            const float* pixel = &raw[4 * (size_t(local.y()) * eye.width + local.x())];
            for (int c = 0; c < 4; ++c) {
                const auto& name = m_data->stereoChannels[i][c];
                if (!name.empty())
                    text << " " << name << ": " << PixelDiagnostics::sampleText(pixel[c])
                         << sampleLocationInfo(eye, c, local.x(), local.y());
            }
        }
        return text.str();
    }
    const size_t index = size_t(y) * width() + x;
    const float* pixel = &(m_data->deep ? getDisplayPixels() : getRawPixels())[4 * index];
    std::stringstream text;
    if (resolutionLevelCount() > 1) text << "Level " << resolutionLevel().toString() << " | ";
    text << "x: " << x + getDataWindow().x()
         << " y: " << y + getDataWindow().y() << " |";
    if (m_data->deep) {
        if (!m_data->covers(index)) return text.str() + " No samples";
        text << " Composite";
    }
    const auto names = rawChannelNames();
    const auto components = rawChannelComponents();
    for (size_t c = 0; c < names.size(); ++c)
        text << " " << names[c] << ": "
             << PixelDiagnostics::sampleText(pixel[components[c]])
             << sampleLocationInfo(c, x, y);
    if (m_layerType == Layer_RGB || m_layerType == Layer_YC) {
        const float* display = &getDisplayPixels()[4 * (size_t(y) * width() + x)];
        text << " | Luminance: " << std::setprecision(9)
             << ToneMapping::luminance(display[0], display[1], display[2]);
    }
    return text.str();
}

float RGBFramebufferModel::component(int x, int y, int channel) const
{
    if (!isImageLoaded() || isDerivedPreview() || x < 0 || x >= width() || y < 0 || y >= height())
        return 0.f;
    const auto& pixels = m_data->deep || (m_layerType == Layer_YC && channel < 3)
                           ? getDisplayPixels() : getRawPixels();
    return pixels[4 * (size_t(y) * width() + x) + channel];
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
    std::vector<std::string> names;
    for (const auto& channel : m_channels)
        if (!channel.empty()) names.push_back(channel);
    return names;
}

std::vector<int> RGBFramebufferModel::rawChannelComponents() const
{
    std::vector<int> components;
    for (size_t i = 0; i < m_channels.size(); ++i)
        if (!m_channels[i].empty()) components.push_back(int(i));
    return components;
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
    if (!m_falseColorAutomatic) updateImage();
}
void RGBFramebufferModel::setFalseColorAutomatic(bool enabled)
{
    if (m_falseColorAutomatic == enabled) return;
    m_falseColorAutomatic = enabled;
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
    const auto                 source   = m_data;
    const auto projection = projectionInput();
    const auto range = depthRange();
    const bool automatic = m_falseColorAutomatic;
    const auto                 mode     = m_previewMode;
    const auto                 method   = m_toneMappingMethod;
    const double exposure = std::exp2(m_exposure);
    const std::array<double, 4> params = {
      {m_toneParams[0], m_toneParams[1], m_toneParams[2], m_toneParams[3]}};
    const double minimum  = m_falseColorMin;
    const double maximum  = m_falseColorMax;
    const auto   colormap = m_falseColorMap;
    requestRender(
      [source, projection, range, automatic, mode, method, exposure, params, minimum, maximum, colormap](
        const Cancellation& cancel) -> RenderResult {
          const auto data = DeepPreview::compose(source, range, cancel);
          if (!data || cancel->load()) return {};
          const double low = automatic && data->hasFiniteLuminance ? data->luminanceMin : minimum;
          const double high = automatic && data->hasFiniteLuminance ? data->luminanceMax : maximum;
          const auto mapper = [mode, method, exposure, params, colormap, low, high](
            const FramebufferData& frame, const Cancellation& cancel) -> QImage {
              const auto* data = &frame;
              QImage image(data->width, data->height, QImage::Format_RGBA8888);
              if (image.isNull()) return image;
              uchar*     bits    = image.bits();
              const auto stride  = image.bytesPerLine();
              const int  threads = renderThreadCount();
              Q_UNUSED(threads);
              const auto mapColor = [&](const float* pixel, uchar* output) {
                  if (mode == Preview_FalseColor) {
                      float rgb[3];
                      colormap->getRGBValue(ToneMapping::luminance(pixel[0], pixel[1], pixel[2]),
                                           low, high, rgb);
                      for (int c = 0; c < 3; ++c) output[c] = ToneMapping::toByte(rgb[c]);
                  } else {
                      for (int c = 0; c < 3; ++c) {
                          const double value = mode == Preview_ToneMapping
                            ? ToneMapping::toSrgb(pixel[c], method, params[0], params[1], params[2], params[3])
                            : ColorTransform::to_sRGB(exposure * pixel[c]);
                          output[c] = ToneMapping::toByte(value);
                      }
                  }
              };
#pragma omp parallel for num_threads(                                          \
    threads) if (size_t(data->width) * data->height >= 262144)
              for (int y = 0; y < data->height; ++y) {
                  if (cancel->load()) continue;
                  uchar*       line = bits + size_t(y) * stride;
                  for (int x = 0; x < data->width; ++x) {
                      uchar* output = line + 4 * x;
                      if (data->stereo[0]) {
                          const QPoint position = data->dataWindow.topLeft() + QPoint(x, y);
                          uchar eyes[2][3] = {};
                          bool covered = false;
                          for (size_t i = 0; i < 2; ++i) {
                              const auto& eye = *data->stereo[i];
                              if (!eye.dataWindow.contains(position)) continue;
                              const QPoint local = position - eye.dataWindow.topLeft();
                              if (!eye.covers(size_t(local.y()) * eye.width + local.x())) continue;
                              mapColor(&eye.pixels[4 * (size_t(local.y()) * eye.width + local.x())], eyes[i]);
                              covered = true;
                          }
                          output[0] = eyes[0][0];
                          output[1] = eyes[1][1];
                          output[2] = eyes[1][2];
                          output[3] = covered ? 255 : 0;
                      } else {
                          if (!data->covers(size_t(y) * data->width + x)) {
                              std::fill(output, output + 4, 0); continue;
                          }
                          mapColor(&data->pixels[4 * (size_t(y) * data->width + x)], output);
                          // Premultiplied color over black is already RGB.
                          output[3] = 255;
                      }
                  }
              }
              return cancel->load() ? QImage() : image;
          };
          return renderProjection(data, projection, mapper, cancel);
      });
}
