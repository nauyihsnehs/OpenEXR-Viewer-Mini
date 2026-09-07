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
  const std::shared_ptr<ExrInput>& file, int partId)
{
    m_partID                                  = partId;
    const std::array<std::string, 4> channels = {{m_layer, "", "", ""}};
    startLoading([file, partId, channels](const Cancellation& cancel) {
        return FramebufferLoader::decode(
          file,
          partId,
          FramebufferLoader::Scalar,
          channels,
          cancel);
    });
}

std::string YFramebufferModel::getColorInfo(int x, int y) const
{
    if (!isImageLoaded() || x < 0 || x >= width() || y < 0 || y >= height())
        return "";
    std::stringstream text;
    text << "x: " << x << " y: " << y
         << " | value = " << getRawPixels()[size_t(y) * width() + x];
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
    const auto   data     = m_data;
    const double minimum  = m_min;
    const double maximum  = m_max;
    const auto   colormap = m_cmap;
    requestRender(
      [data, minimum, maximum, colormap](const Cancellation& cancel) {
          QImage image(data->width, data->height, QImage::Format_RGB888);
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
                  float rgb[3];
                  colormap->getRGBValue(
                    data->pixels[size_t(y) * data->width + x],
                    minimum,
                    maximum,
                    rgb);
                  for (int c = 0; c < 3; ++c)
                      line[3 * x + c] = ToneMapping::toByte(rgb[c]);
              }
          }
          return cancel->load() ? QImage() : image;
      });
}
