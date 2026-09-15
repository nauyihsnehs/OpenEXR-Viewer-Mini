#pragma once

#include <QRect>
#include <QString>
#include <OpenEXR/ImfChromaticities.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// Published once by a decoder, then shared read-only by previews and exports.
struct FramebufferData {
    int                width       = 0;
    int                height      = 0;
    float              pixelAspect = 1.f;
    QRect              dataWindow;
    QRect              displayWindow;
    std::vector<float> pixels;
    // Only populated when RGB gamut conversion changes the display buffer.
    std::vector<float> sourcePixels;
    bool               hasRawChromaticities = false;
    Imf::Chromaticities rawChromaticities;
    // Source-channel flags, before color conversion or display mapping.
    enum NonFiniteFlag { NaN = 1, PositiveInf = 2, NegativeInf = 4 };
    struct AnomalyRegion {
        QRect bounds; // Pixel edges in framebuffer-local coordinates.
        uint64_t pixelCount = 0;
        uint8_t flags = 0;
    };
    std::vector<AnomalyRegion> anomalyRegions;
    double             minimum            = 0.;
    double             maximum            = 0.;
    uint64_t           nanCount           = 0;
    uint64_t           infCount           = 0;
    uint64_t           positiveInfCount   = 0;
    uint64_t           negativeInfCount   = 0;
    bool               hasFiniteSamples   = false;
    double             luminanceMin       = 0.;
    double             luminanceMax       = 0.;
    bool               hasFiniteLuminance = false;
};

using Cancellation = std::shared_ptr<std::atomic_bool>;

struct DecodeResult {
    std::shared_ptr<const FramebufferData> data;
    QString                                error;
};
