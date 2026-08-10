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

    float         luminance(float r, float g, float b);
    unsigned char toByte(float value);
    float
    toSrgb(float value, Method method, float p0, float p1, float p2, float p3);
}   // namespace ToneMapping
