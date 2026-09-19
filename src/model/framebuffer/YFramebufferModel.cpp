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

#include "YFramebufferModel.h"
#include "DeepPreview.h"
#include <algorithm>
#include "PixelDiagnostics.h"
#include "FramebufferLoader.h"
#include "ToneMapping.h"
#include <cmath>
#include <sstream>

YFramebufferModel::YFramebufferModel(
  const std::string& layerName, QObject* parent)
  : FramebufferModel(parent)
  , m_partID(0)
  , m_layer(layerName)
  , m_min(0.)
  , m_max(1.)
  , m_cmap(ColormapModule::create("grayscale"))
{}
YFramebufferModel::~YFramebufferModel() = default;

void YFramebufferModel::load(
  const std::shared_ptr<ExrInput>& file, int partId, ResolutionLevel level)
{
    m_partID                                  = partId;
    const std::array<std::string, 4> channels = {{m_layer, "", "", ""}};
    startLoading([file, partId, channels, level](const Cancellation& cancel) {
        return FramebufferLoader::decode(
          file,
          partId,
          FramebufferLoader::Scalar,
          channels,
          cancel, level);
    });
}

std::string YFramebufferModel::getColorInfo(int x, int y, bool compact) const
{
    if (isProjected()) return projectedColorInfo(x, y, compact);
    if (!isImageLoaded() || x < 0 || x >= width() || y < 0 || y >= height())
        return "";
    std::stringstream text;
    if (compact) {
        const size_t index = size_t(y) * width() + x;
        text << "(" << x + getDataWindow().x() << ", " << y + getDataWindow().y() << ")  ";
        if (m_data->deep && !m_data->covers(index)) return text.str() + "—";
        const std::string name = PixelDiagnostics::compactChannelName(m_layer);
        if (m_data->deep) text << (m_layer == "Z" ? "Nearest " : "Comp ");
        text << name << " " << PixelDiagnostics::sampleText(
          m_data->deep ? m_data->pixels[index] : getRawPixels()[index], true);
        return text.str();
    }
    if (resolutionLevelCount() > 1) text << "Level " << resolutionLevel().toString() << " | ";
    const size_t index = size_t(y) * width() + x;
    if (m_data->deep) {
        text << "x: " << x + getDataWindow().x() << " y: " << y + getDataWindow().y() << " | ";
        if (!m_data->covers(index)) return text.str() + "No samples";
        text << (m_layer == "Z" ? "Nearest Z: " : "Composite " + m_layer + ": ")
             << PixelDiagnostics::sampleText(m_data->pixels[index]);
        return text.str();
    }
    text << "x: " << x + getDataWindow().x()
         << " y: " << y + getDataWindow().y()
         << " | " << m_layer << ": "
         << PixelDiagnostics::sampleText(getRawPixels()[size_t(y) * width() + x])
         << sampleLocationInfo(0, x, y);
    return text.str();
}
std::vector<std::string> YFramebufferModel::rawChannelNames() const
{
    return {m_layer};
}
void YFramebufferModel::setMinValue(double value)
{
    setRange(value, m_max);
}
void YFramebufferModel::setMaxValue(double value)
{
    setRange(m_min, value);
}
void YFramebufferModel::setRange(double min, double max)
{
    if (!std::isfinite(min) || !std::isfinite(max) || min > max) return;
    if (m_min == min && m_max == max) return;
    m_min = min;
    m_max = max;
    if (!m_automaticRange) updateImage();
}
void YFramebufferModel::setAutomaticRange(bool enabled)
{
    if (m_automaticRange == enabled) return;
    m_automaticRange = enabled;
    updateImage();
}
void YFramebufferModel::setColormap(ColormapModule::Map map)
{
    m_cmap.reset(ColormapModule::create(map));
    updateImage();
}
void YFramebufferModel::updateImage()
{
    if (!isImageLoaded()) return;
    const auto   source   = m_data;
    const auto projection = projectionInput();
    const auto range = depthRange();
    const bool automatic = m_automaticRange;
    const double minimum  = m_min;
    const double maximum  = m_max;
    const auto   colormap = m_cmap;
    requestRender(
      [source, projection, range, automatic, minimum, maximum, colormap](const Cancellation& cancel) -> RenderResult {
          const auto data = DeepPreview::compose(source, range, cancel);
          if (!data || cancel->load()) return {};
          const bool finite = data->deep ? data->hasFiniteDisplay : data->hasFiniteSamples;
          const double low = automatic && finite ? (data->deep ? data->displayMinimum : data->minimum) : minimum;
          const double high = automatic && finite ? (data->deep ? data->displayMaximum : data->maximum) : maximum;
          const auto mapper = [colormap, low, high](
            const FramebufferData& frame, const Cancellation& cancel) -> QImage {
              const auto* data = &frame;
              const int components = data->deep || !data->deepCoverage.empty() ? 4 : 3;
              QImage image(data->width, data->height, components == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
              if (image.isNull()) return image;
              uchar*     bits    = image.bits();
              const auto stride  = image.bytesPerLine();
              const int  threads = renderThreadCount();
              Q_UNUSED(threads);
#pragma omp parallel for num_threads(threads) if (data->pixels.size() >= 262144)
              for (int y = 0; y < data->height; ++y) {
                  if (cancel->load()) continue;
                  uchar* line = bits + size_t(y) * stride;
                  for (int x = 0; x < data->width; ++x) {
                      uchar* output = line + components * x;
                      if (!data->covers(size_t(y) * data->width + x)) {
                          std::fill(output, output + components, 0); continue;
                      }
                      float rgb[3];
                      colormap->getRGBValue(
                        data->pixels[size_t(y) * data->width + x],
                        low,
                        high,
                        rgb);
                      for (int c = 0; c < 3; ++c)
                          output[c] = ToneMapping::toByte(rgb[c]);
                      if (components == 4) output[3] = 255;
                  }
              }
              return cancel->load() ? QImage() : image;
          };
          return renderProjection(data, projection, mapper, cancel);
      });
}
