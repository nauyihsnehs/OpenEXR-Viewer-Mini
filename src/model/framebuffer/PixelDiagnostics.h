#pragma once

#include "FramebufferData.h"
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

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

    inline std::string compactSampleText(double value)
    {
        if (std::isnan(value)) return "NaN";
        if (std::isinf(value)) return value > 0. ? "+Inf" : "-Inf";
        std::ostringstream text;
        text.imbue(std::locale::classic());
        const bool small = value != 0. && std::abs(value) < 1.;
        text << (small ? std::scientific : std::fixed) << std::setprecision(2) << value;
        return text.str();
    }

    inline std::string sampleText(float value, bool compact = false)
    {
        if (compact) return compactSampleText(value);
        if (std::isnan(value)) return "NaN";
        if (std::isinf(value)) return value > 0.f ? "+Inf" : "-Inf";
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<float>::max_digits10)
             << value;
        return text.str();
    }

    inline std::string compactChannelName(const std::string& name)
    {
        const auto dot = name.rfind('.');
        const std::string suffix = dot == std::string::npos ? name : name.substr(dot + 1);
        if (suffix == "R" || suffix == "G" || suffix == "B" || suffix == "A"
            || suffix == "Y" || suffix == "RY" || suffix == "BY" || suffix == "Z")
            return suffix;
        return name;
    }

    inline std::string compactChannels(const std::vector<std::string>& names,
                                       const std::vector<int>& components,
                                       const float* pixel)
    {
        std::vector<std::string> labels;
        for (const auto& name : names) labels.push_back(compactChannelName(name));
        const bool rgb = labels == std::vector<std::string>{"R", "G", "B"};
        const bool rgba = labels == std::vector<std::string>{"R", "G", "B", "A"};
        std::ostringstream text;
        if (rgb || rgba) text << (rgba ? "RGBA " : "RGB ");
        for (size_t c = 0; c < labels.size(); ++c) {
            if (c) text << (rgb || rgba ? " / " : "  ");
            if (!rgb && !rgba) text << labels[c] << " ";
            text << sampleText(pixel[components[c]], true);
        }
        return text.str();
    }
}
