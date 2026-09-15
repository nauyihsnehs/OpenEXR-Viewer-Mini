#pragma once

#include "FramebufferData.h"
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace PixelDiagnostics
{
    inline uint8_t classify(float value)
    {
        if (std::isnan(value)) return FramebufferData::NaN;
        if (std::isinf(value))
            return value > 0.f ? FramebufferData::PositiveInf
                               : FramebufferData::NegativeInf;
        return 0;
    }

    std::vector<FramebufferData::AnomalyRegion> connectedRegions(
      const std::vector<uint8_t>& flags, int width, int height,
      const Cancellation& cancel);

    inline std::string sampleText(float value)
    {
        if (std::isnan(value)) return "NaN";
        if (std::isinf(value)) return value > 0.f ? "+Inf" : "-Inf";
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<float>::max_digits10)
             << value;
        return text.str();
    }
}
