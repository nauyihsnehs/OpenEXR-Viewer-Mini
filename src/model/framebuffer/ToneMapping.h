#pragma once

namespace ToneMapping
{
    enum Method
    {
        Reinhard,
        Aces,
        Filmic,
        Logarithmic,
        Clamp,
    };

    double         luminance(double r, double g, double b);
    unsigned char toByte(double value);
    double
    toSrgb(double value, Method method, double p0, double p1, double p2, double p3);
}   // namespace ToneMapping
