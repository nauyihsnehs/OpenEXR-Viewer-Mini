#pragma once

#include <QRect>
#include <QString>
#include <atomic>
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
    double             minimum            = 0.;
    double             maximum            = 0.;
    uint64_t           nanCount           = 0;
    uint64_t           infCount           = 0;
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
