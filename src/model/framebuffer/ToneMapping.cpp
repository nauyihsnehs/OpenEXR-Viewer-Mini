#include "ToneMapping.h"

#include <util/ColorTransform.h>

#include <algorithm>
#include <cmath>

namespace
{
    double clamp(double value, double minimum, double maximum)
    {
        if (value < minimum) return minimum;
        if (value > maximum) return maximum;
        return value;
    }

    double safeInput(double value)
    {
        if (!std::isfinite(value) || value < 0.) return 0.;
        return value;
    }

    double positive(double value)
    {
        return value > 0.001 ? value : 0.001;
    }

    double reinhard(double value, double key, double shoulder)
    {
        const double scaled  = value * positive(key);
        const double rolloff = positive(shoulder);
        return scaled * (1. + scaled / (rolloff * rolloff)) / (1. + scaled);
    }

    double acesFitted(double value, double shoulder, double toe)
    {
        const double a = 2.51;
        const double b = 0.03 * positive(toe);
        const double c = 2.43 * positive(shoulder);
        const double d = 0.59;
        const double e = 0.14 * positive(toe);
        return value * (a * value + b) / (value * (c * value + d) + e);
    }

    double hablePartial(
      double value, double shoulder, double linear, double angle, double toe)
    {
        const double b = positive(linear);
        const double d = positive(toe);
        const double e = 0.02;
        const double f = 0.30;
        return (value * (shoulder * value + angle * b) + d * e)
                 / (value * (shoulder * value + b) + d * f)
               - e / f;
    }

    double
    filmic(double value, double shoulder, double linear, double angle, double toe)
    {
        const double white = hablePartial(11.2, shoulder, linear, angle, toe);
        if (!std::isfinite(white) || white <= 0.) return 0.;
        return hablePartial(value, shoulder, linear, angle, toe) / white;
    }

    double logarithmic(double value, double range, double compression)
    {
        const double amount = positive(compression);
        const double scale  = std::log1p(positive(range) * amount);
        if (!std::isfinite(scale) || scale <= 0.) return 0.;
        return std::log1p(value * amount) / scale;
    }

    double clipped(double value, double minimum, double maximum)
    {
        if (maximum <= minimum) return 0.;
        return (clamp(value, minimum, maximum) - minimum) / (maximum - minimum);
    }

    double apply(
      double               value,
      ToneMapping::Method method,
      double               p0,
      double               p1,
      double               p2,
      double               p3)
    {
        switch (method) {
            case ToneMapping::Aces:
                return acesFitted(value, p0, p1);
            case ToneMapping::Filmic:
                return filmic(value, p0, p1, p2, p3);
            case ToneMapping::Logarithmic:
                return logarithmic(value, p0, p1);
            case ToneMapping::Clamp:
                return clipped(value, p0, p1);
            case ToneMapping::Reinhard:
            default:
                return reinhard(value, p0, p1);
        }
    }
}   // namespace

namespace ToneMapping
{
    double luminance(double r, double g, double b)
    {
        return 0.2126 * r + 0.7152 * g + 0.0722 * b;
    }

    unsigned char toByte(double value)
    {
        if (std::isnan(value) || value <= 0.) return 0;
        if (value >= 1.) return 255;
        return static_cast<unsigned char>(255. * value);
    }

    double
    toSrgb(double value, Method method, double p0, double p1, double p2, double p3)
    {
        if (std::isnan(value)) return 0.;
        if (std::isinf(value)) return value > 0. ? 1. : 0.;
        double mapped = apply(safeInput(value), method, p0, p1, p2, p3);
        if (std::isnan(mapped)) mapped = 0.;
        return ColorTransform::to_sRGB(clamp(mapped, 0., 1.));
    }
}   // namespace ToneMapping
