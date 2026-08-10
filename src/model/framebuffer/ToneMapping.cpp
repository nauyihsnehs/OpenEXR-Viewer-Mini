#include "ToneMapping.h"

#include <util/ColorTransform.h>

#include <algorithm>
#include <cmath>

namespace
{
    float clamp(float value, float minimum, float maximum)
    {
        if (value < minimum) return minimum;
        if (value > maximum) return maximum;
        return value;
    }

    float safeInput(float value)
    {
        if (!std::isfinite(value) || value < 0.f) return 0.f;
        return clamp(value, 0.f, 1.e10f);
    }

    float positive(float value)
    {
        return value > 0.001f ? value : 0.001f;
    }

    float reinhard(float value, float key, float shoulder)
    {
        const float scaled  = value * positive(key);
        const float rolloff = positive(shoulder);
        return scaled * (1.f + scaled / (rolloff * rolloff)) / (1.f + scaled);
    }

    float acesFitted(float value, float shoulder, float toe)
    {
        const float a = 2.51f;
        const float b = 0.03f * positive(toe);
        const float c = 2.43f * positive(shoulder);
        const float d = 0.59f;
        const float e = 0.14f * positive(toe);
        return value * (a * value + b) / (value * (c * value + d) + e);
    }

    float hablePartial(
      float value, float shoulder, float linear, float angle, float toe)
    {
        const float b = positive(linear);
        const float d = positive(toe);
        const float e = 0.02f;
        const float f = 0.30f;
        return (value * (shoulder * value + angle * b) + d * e)
                 / (value * (shoulder * value + b) + d * f)
               - e / f;
    }

    float
    filmic(float value, float shoulder, float linear, float angle, float toe)
    {
        const float white = hablePartial(11.2f, shoulder, linear, angle, toe);
        if (!std::isfinite(white) || white <= 0.f) return 0.f;
        return hablePartial(value, shoulder, linear, angle, toe) / white;
    }

    float logarithmic(float value, float range, float compression)
    {
        const float amount = positive(compression);
        const float scale  = std::log1p(positive(range) * amount);
        if (!std::isfinite(scale) || scale <= 0.f) return 0.f;
        return std::log1p(value * amount) / scale;
    }

    float clipped(float value, float minimum, float maximum)
    {
        const float upper = maximum > minimum ? maximum : minimum + 0.001f;
        return (clamp(value, minimum, upper) - minimum) / (upper - minimum);
    }

    float apply(
      float               value,
      ToneMapping::Method method,
      float               p0,
      float               p1,
      float               p2,
      float               p3)
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
    float luminance(float r, float g, float b)
    {
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }

    unsigned char toByte(float value)
    {
        if (!std::isfinite(value)) return 0;
        return static_cast<unsigned char>(
          std::max(0, std::min(255, int(255.f * value))));
    }

    float
    toSrgb(float value, Method method, float p0, float p1, float p2, float p3)
    {
        float mapped = apply(safeInput(value), method, p0, p1, p2, p3);
        if (!std::isfinite(mapped)) mapped = 0.f;
        return ColorTransform::to_sRGB(clamp(mapped, 0.f, 1.f));
    }
}   // namespace ToneMapping
