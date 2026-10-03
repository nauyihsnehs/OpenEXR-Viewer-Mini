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

#include "Colormap.h"
#include "ColormapData.h"
#include <stdexcept>

namespace {
const char* const names[] = {"grayscale", "bbgr", "turbo", "magma", "inferno", "plasma", "viridis"};
const char* const labels[] = {"Grayscale", "BBGR", "Turbo", "Magma", "Inferno", "Plasma", "Viridis"};

class Grayscale : public Colormap {
  public:
    void getRGBValue(float value, float rgb[3]) const override
    {
        rgb[0] = rgb[1] = rgb[2] = value;
    }
};

class BbgrMap : public Colormap {
  public:
    void getRGBValue(float value, float rgb[3]) const override
    {
        static const float colors[][3] = {
          {0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 1.f},
          {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}, {1.f, 0.f, 0.f}};
        // Keep the original knot arithmetic and interpolation, without allocating per pixel.
        value = std::min(std::max(value, 0.f), 1.f);
        for (int i = 1; i < 6; ++i) {
            const float low = float(i - 1) / 5.f, high = float(i) / 5.f;
            if (value <= high) {
                const float a = (std::min(std::max(value, low), high) - low) / (high - low);
                for (int c = 0; c < 3; ++c)
                    rgb[c] = a * colors[i][c] + (1.f - a) * colors[i - 1][c];
                return;
            }
        }
        std::copy(colors[5], colors[5] + 3, rgb);
    }
};

class Tabulated : public Colormap {
  public:
    explicit Tabulated(const float* values) : values(values) {}
    void getRGBValue(float value, float rgb[3]) const override
    {
        value = std::max(0.f, std::min(1.f, value));
        const int low = int(std::floor(value * 255.f));
#ifdef TAB_COLORMAP_CLOSEST
        std::copy(values + 3 * low, values + 3 * low + 3, rgb);
#else
        const int high = std::min(255, low + 1);
        const float a = value * 255.f - low;
        for (int c = 0; c < 3; ++c)
            rgb[c] = values[3 * low + c] + (values[3 * high + c] - values[3 * low + c]) * a;
#endif
    }
  private:
    const float* values;
};
} // namespace

Colormap* ColormapModule::create(const std::string& name)
{
    for (int i = 0; i < N_MAPS; ++i)
        if (name == names[i]) return create(static_cast<Map>(i));
    throw std::invalid_argument("Unknown colormap: " + name);
}

Colormap* ColormapModule::create(Map map)
{
    static const float* const tables[] = {
      turbo_colormap_data, magma_colormap_data, inferno_colormap_data,
      plasma_colormap_data, viridis_colormap_data};
    if (map == GRAYSCALE) return new Grayscale;
    if (map == BBGR) return new BbgrMap;
    if (map >= TURBO && map <= VIRIDIS) return new Tabulated(tables[map - TURBO]);
    throw std::invalid_argument("Invalid colormap index.");
}

std::string ColormapModule::toString(Map map)
{
    if (map < GRAYSCALE || map >= N_MAPS)
        throw std::invalid_argument("Invalid colormap index.");
    return labels[map];
}
