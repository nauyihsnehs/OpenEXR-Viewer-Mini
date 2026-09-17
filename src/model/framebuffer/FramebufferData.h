#pragma once

#include <QRect>
#include <QString>
#include <OpenEXR/ImfChromaticities.h>
#include <util/ViewMetadata.h>
#include <model/DeepSamples.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// Published once by a decoder, then shared read-only by previews and exports.
struct FramebufferData {
    int envmap = -1;
    bool completeEnvironment = false;
    ResolutionLevel resolutionLevel;
    std::vector<ResolutionLevel> resolutionLevels = {{0, 0}};
    int                width       = 0;
    int                height      = 0;
    float              pixelAspect = 1.f;
    QRect              dataWindow;
    QRect              displayWindow;
    std::vector<float> pixels;
    // Populated when YC reconstruction or RGB gamut conversion changes values.
    std::vector<float> sourcePixels;
    std::shared_ptr<const DeepSamples> deep;
    std::array<std::string, 4> deepChannels;
    bool deepScalar = false;
    DepthRange depthRange;
    // Present-sample mask for Deep composites and projected canvases (sphere exterior).
    std::vector<uint8_t> deepCoverage;
    double displayMinimum = 0., displayMaximum = 0.;
    bool hasFiniteDisplay = false;
    bool hasDeep() const
    { return bool(deep) || (stereo[0] && (stereo[0]->hasDeep() || stereo[1]->hasDeep())); }
    bool covers(size_t pixel) const
    { return deepCoverage.empty() ? !deep : deepCoverage[pixel] != 0; }
    std::array<QPoint, 4> sourceSampling = {{QPoint(1, 1), QPoint(1, 1),
                                           QPoint(1, 1), QPoint(1, 1)}};
    bool               hasRawChromaticities = false;
    Imf::Chromaticities rawChromaticities;
    ViewMetadata       rawViews;
    // Only derived stereo previews carry these immutable, independently decoded sources.
    std::array<std::shared_ptr<const FramebufferData>, 2> stereo;
    std::array<std::array<std::string, 4>, 2> stereoChannels;
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
